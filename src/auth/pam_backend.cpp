// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "auth/pam_backend.h"
#include "base/i18n.h"
#include "core/main_loop.h"
#include <glob.h>
#include <pwd.h>
#include <unistd.h>

using cutefish::i18n::tr;

namespace cutefish::auth {

namespace {

// pam_fprintd comes from libpam-fprintd, which is not a dependency: without it
// the reader is simply never offered.
bool fingerprintModulePresent()
{
    for (const char *pattern : {"/usr/lib/*/security/pam_fprintd.so",
                                "/usr/lib/security/pam_fprintd.so",
                                "/lib/*/security/pam_fprintd.so",
                                "/lib/security/pam_fprintd.so"}) {
        glob_t found {};
        const bool matched = glob(pattern, 0, nullptr, &found) == 0 && found.gl_pathc > 0;
        globfree(&found);
        if (matched)
            return true;
    }
    return false;
}

const char *const fprintService = "net.reactivated.Fprint";

// Kept across locks, so the next lock opens on the right prompt before fprintd
// has answered again.
bool lastKnownEnrolled = false;

// cutefish-auth-helper's exit status for a finger that was read and did not match.
constexpr int kFingerprintNoMatch = 2;

// Soon enough after a finger that did not match that a second touch is not lost.
constexpr std::chrono::milliseconds kFingerprintRetryDelay(500);

// libfprint disables a reader that has been verifying for a few minutes as
// overheating, so it is kept verifying only this long after the user was last
// seen, and an attempt that ends is not followed at once by another.
constexpr std::chrono::seconds kFingerprintKeepAlive(60);
constexpr std::chrono::seconds kFingerprintRest(2);

// pam_fprintd says nothing when it has no reader to use: no fprintd, no
// device, or no enrolled finger.
constexpr std::chrono::seconds kFingerprintMissingRest(60);

} // namespace

PamBackend::PamBackend()
    : m_timeout(core::mainLoop())
    , m_fingerprintRetry(core::mainLoop())
    , m_logind(base::dbus::Connection::system(core::mainLoop()), "org.freedesktop.login1",
               "/org/freedesktop/login1", "org.freedesktop.login1.Manager")
{
    m_timeout.setSingleShot(true);
    m_timeout.onTimeout = [this] {
        reset();
        failed(tr("Authentication timed out"));
    };
    m_fingerprintRetry.setSingleShot(true);
    m_fingerprintRetry.onTimeout = [this] {
        if (std::chrono::steady_clock::now() < m_fingerprintWantedUntil)
            launchFingerprint();
    };
    m_prepareForSleep =
            m_logind.onSignal("PrepareForSleep", [this](const base::dbus::Message &message) {
                bool sleeping = false;
                if (!message.read(sleeping))
                    return;
                stopFingerprint();
                if (!sleeping)
                    systemWoke();
            });
}

PamBackend::~PamBackend()
{
    reset();
    stopFingerprint();
}

std::string PamBackend::defaultUserName() const
{
    const passwd *user = getpwuid(getuid());
    return user && user->pw_name ? std::string(user->pw_name) : std::string();
}

void PamBackend::authenticate(const std::string &userName, Secret secret)
{
    if (m_pending || (m_process && m_process->isRunning()))
        return;
    // A wrong password is told by the field shaking, not in words.
    if (userName != defaultUserName() || secret.size() > 4096 || secret.isEmpty()) {
        failed({});
        return;
    }
    m_pending = true;
    m_process = std::make_unique<base::Process>(core::mainLoop());
    // A crash is the service failing, not a verdict.
    m_process->onFinished = [this](int code, bool crashed) {
        if (!m_pending)
            return;
        m_pending = false;
        m_timeout.stop();
        if (crashed)
            failed(tr("Authentication service unavailable"));
        else if (code == 0)
            succeeded();
        else
            failed({});
    };
    // The helper reads only this inherited pipe, never argv or a public socket.
    m_process->setStandardInput(std::string(secret.constData(), secret.size()));
    secret.clear();
    if (!m_process->start(CUTEFISH_AUTH_HELPER)) {
        m_pending = false;
        m_process.reset();
        failed(tr("Authentication service unavailable"));
        return;
    }
    m_timeout.start(std::chrono::milliseconds(30000));
}

void PamBackend::reset()
{
    m_pending = false;
    m_timeout.stop();
    m_process.reset();
}

bool PamBackend::fingerprintAvailable(const std::string &) const
{
    static const bool present = fingerprintModulePresent();
    return present;
}

bool PamBackend::fingerprintEnrolled() const
{
    return lastKnownEnrolled;
}

void PamBackend::checkFingerprintEnrollment(const std::string &userName)
{
    m_enrollmentFingers.clear();
    if (!fingerprintAvailable(userName)) {
        setFingerprintEnrolled(false);
        return;
    }
    base::dbus::Connection *bus = m_logind.connection();
    const base::dbus::Proxy manager(bus, fprintService, "/net/reactivated/Fprint/Manager",
                                    "net.reactivated.Fprint.Manager");
    m_enrollmentDevices = manager.callAsync(
            "GetDevices", [this, bus, userName](const base::dbus::Reply &reply) {
                base::dbus::PendingCall done = std::move(m_enrollmentDevices);
                const auto devices = reply.value<std::vector<base::dbus::ObjectPath>>();
                if (!devices || devices->empty()) {
                    setFingerprintEnrolled(false);
                    return;
                }
                m_enrollmentFound = false;
                m_enrollmentUnanswered = devices->size();
                for (const base::dbus::ObjectPath &device : *devices) {
                    const base::dbus::Proxy proxy(bus, fprintService, device.path,
                                                  "net.reactivated.Fprint.Device");
                    // A device without a print for the user answers with an error.
                    m_enrollmentFingers.push_back(proxy.callAsync(
                            "ListEnrolledFingers",
                            [this](const base::dbus::Reply &fingers) {
                                const auto names = fingers.value<std::vector<std::string>>();
                                if (names && !names->empty())
                                    m_enrollmentFound = true;
                                if (--m_enrollmentUnanswered == 0)
                                    setFingerprintEnrolled(m_enrollmentFound);
                            },
                            userName));
                }
            });
}

void PamBackend::setFingerprintEnrolled(bool enrolled)
{
    if (lastKnownEnrolled == enrolled)
        return;
    lastKnownEnrolled = enrolled;
    fingerprintEnrollmentChanged();
}

void PamBackend::startFingerprint(const std::string &userName)
{
    if (!fingerprintAvailable(userName))
        return;
    const auto now = std::chrono::steady_clock::now();
    m_fingerprintWantedUntil = now + kFingerprintKeepAlive;
    if (m_fingerprint || m_fingerprintRetry.isActive())
        return;
    if (now < m_fingerprintRestsUntil) {
        m_fingerprintRetry.start(m_fingerprintRestsUntil - now);
        return;
    }
    launchFingerprint();
}

void PamBackend::launchFingerprint()
{
    m_fingerprint = std::make_unique<base::Process>(core::mainLoop());
    m_fingerprintSpoke = false;
    // A stopped attempt can still report; only the current one counts.
    const base::Process *process = m_fingerprint.get();
    // The helper writes one notice per line; chunks need not end at a newline.
    auto partial = std::make_shared<std::string>();
    m_fingerprint->onStandardError = [this, process, partial](std::string_view text) {
        if (m_fingerprint.get() != process)
            return;
        partial->append(text);
        size_t newline = 0;
        while ((newline = partial->find('\n')) != std::string::npos) {
            std::string line = partial->substr(0, newline);
            partial->erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            // The reader speaks only once it has started verifying.
            if (!line.empty())
                m_fingerprintSpoke = true;
        }
    };
    m_fingerprint->onFinished = [this, process](int code, bool crashed) {
        if (m_fingerprint.get() != process)
            return;
        // Out of m_fingerprint before anything a handler might stop.
        m_lastFingerprint = std::move(m_fingerprint);
        const auto now = std::chrono::steady_clock::now();
        if (!crashed && code == 0) {
            fingerprintSucceeded();
            return;
        }
        if (!crashed && code == kFingerprintNoMatch) {
            // Someone is at the reader.
            m_fingerprintWantedUntil = now + kFingerprintKeepAlive;
            m_fingerprintRetry.start(kFingerprintRetryDelay);
            fingerprintNoMatch();
            return;
        }
        const auto rest = m_fingerprintSpoke ? kFingerprintRest : kFingerprintMissingRest;
        m_fingerprintRestsUntil = now + rest;
        if (now < m_fingerprintWantedUntil)
            m_fingerprintRetry.start(rest);
    };
    if (!m_fingerprint->start(CUTEFISH_AUTH_HELPER, {"--fingerprint"})) {
        m_fingerprint.reset();
        m_fingerprintRestsUntil = std::chrono::steady_clock::now() + kFingerprintMissingRest;
    }
}

void PamBackend::stopFingerprint()
{
    m_fingerprintRetry.stop();
    m_fingerprintRestsUntil = {};
    m_fingerprintWantedUntil = {};
    if (std::unique_ptr<base::Process> process = std::exchange(m_fingerprint, nullptr))
        process->terminate();
}
} // namespace cutefish::auth
