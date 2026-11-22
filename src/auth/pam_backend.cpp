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

} // namespace

PamBackend::PamBackend()
    : m_timeout(core::mainLoop())
{
    m_timeout.setSingleShot(true);
    m_timeout.onTimeout = [this] {
        reset();
        failed(tr("Authentication timed out"));
    };
}

PamBackend::~PamBackend()
{
    reset();
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
    stopFingerprint();
}

bool PamBackend::fingerprintAvailable(const std::string &) const
{
    static const bool present = fingerprintModulePresent();
    return present;
}

void PamBackend::startFingerprint(const std::string &userName)
{
    if (m_fingerprint && !m_fingerprintDone)
        return;
    if (!fingerprintAvailable(userName))
        return;
    m_fingerprint = std::make_unique<base::Process>(core::mainLoop());
    m_fingerprintDone = false;
    // The helper writes one notice per line; chunks need not end at a newline.
    auto partial = std::make_shared<std::string>();
    m_fingerprint->onStandardError = [this, partial](std::string_view text) {
        partial->append(text);
        size_t newline = 0;
        while ((newline = partial->find('\n')) != std::string::npos) {
            std::string line = partial->substr(0, newline);
            partial->erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty()) {
                // The reader speaks only once it has started verifying.
                setFingerprintActive(true);
                fingerprintMessage(line);
            }
        }
    };
    m_fingerprint->onFinished = [this](int code, bool crashed) {
        m_fingerprintDone = true;
        setFingerprintActive(false);
        // A finger that is not placed is not an error: the password stays.
        if (!crashed && code == 0)
            fingerprintSucceeded();
    };
    if (!m_fingerprint->start(CUTEFISH_AUTH_HELPER, {"--fingerprint"})) {
        m_fingerprint.reset();
        m_fingerprintDone = true;
    }
}

void PamBackend::stopFingerprint()
{
    std::unique_ptr<base::Process> process = std::exchange(m_fingerprint, nullptr);
    if (!process)
        return;
    // Set first: ending the program may report its end before it returns.
    m_fingerprintDone = true;
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
