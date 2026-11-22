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

// cutefish-auth-helper's exit status for a finger that was read and did not match.
constexpr int kFingerprintNoMatch = 2;

// Soon enough after a finger that did not match that a second touch is not lost.
constexpr std::chrono::milliseconds kFingerprintRetryDelay(500);

// libfprint disables a reader that has been verifying for a few minutes as
// overheating, so an attempt that ends is not followed at once by another.
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
    m_fingerprintRetry.onTimeout = [this] { launchFingerprint(); };
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
    if (userName != defaultUserName() || secret.size() > 4096 || secret.isEmpty()) {
        failed(tr("Authentication failed"));
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
            failed(tr("Authentication failed"));
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

void PamBackend::startFingerprint(const std::string &userName)
{
    if (m_fingerprintRunning || m_fingerprintRetry.isActive()
        || std::chrono::steady_clock::now() < m_fingerprintRestsUntil)
        return;
    if (!fingerprintAvailable(userName))
        return;
    launchFingerprint();
}

void PamBackend::launchFingerprint()
{
    m_fingerprint = std::make_unique<base::Process>(core::mainLoop());
    m_fingerprintRunning = true;
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
            if (!line.empty()) {
                // The reader speaks only once it has started verifying.
                m_fingerprintSpoke = true;
                setFingerprintActive(true);
                fingerprintMessage(line);
            }
        }
    };
    m_fingerprint->onFinished = [this, process](int code, bool crashed) {
        if (m_fingerprint.get() != process)
            return;
        m_fingerprintRunning = false;
        if (!crashed && code == 0) {
            setFingerprintActive(false);
            fingerprintSucceeded();
            return;
        }
        // Someone is at the reader; the hint holds across the retry.
        if (!crashed && code == kFingerprintNoMatch) {
            m_fingerprintRetry.start(kFingerprintRetryDelay);
            return;
        }
        setFingerprintActive(false);
        m_fingerprintRestsUntil = std::chrono::steady_clock::now()
                + (m_fingerprintSpoke ? kFingerprintRest : kFingerprintMissingRest);
    };
    if (!m_fingerprint->start(CUTEFISH_AUTH_HELPER, {"--fingerprint"})) {
        m_fingerprint.reset();
        m_fingerprintRunning = false;
        m_fingerprintRestsUntil = std::chrono::steady_clock::now() + kFingerprintMissingRest;
        setFingerprintActive(false);
    }
}

void PamBackend::stopFingerprint()
{
    m_fingerprintRetry.stop();
    m_fingerprintRestsUntil = {};
    m_fingerprintRunning = false;
    if (std::unique_ptr<base::Process> process = std::exchange(m_fingerprint, nullptr))
        process->terminate();
    setFingerprintActive(false);
}

void PamBackend::setFingerprintActive(bool active)
{
    if (m_fingerprintActive == active)
        return;
    m_fingerprintActive = active;
    fingerprintActiveChanged(active);
}
} // namespace cutefish::auth
