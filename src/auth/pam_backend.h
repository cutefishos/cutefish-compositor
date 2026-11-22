// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_AUTH_PAM_BACKEND_H
#define CUTEFISH_AUTH_PAM_BACKEND_H
#include "auth/auth_backend.h"
#include "base/dbus.h"
#include "base/process.h"
#include "base/timer.h"
#include <chrono>
#include <memory>
#include <vector>

namespace cutefish::auth {
class PamBackend : public AuthBackend
{
public:
    PamBackend();
    ~PamBackend() override;
    std::string defaultUserName() const override;

    void start() override { }

    void authenticate(const std::string &userName, Secret secret) override;
    void reset() override;

    bool fingerprintAvailable(const std::string &userName) const override;
    void startFingerprint(const std::string &userName) override;
    void stopFingerprint() override;
    bool fingerprintEnrolled() const override;
    void checkFingerprintEnrollment(const std::string &userName) override;

    void accept() override { }

private:
    void launchFingerprint();
    void setFingerprintEnrolled(bool enrolled);

    std::unique_ptr<base::Process> m_process;
    base::Timer m_timeout;
    bool m_pending = false;
    // Set only while an attempt runs.
    std::unique_ptr<base::Process> m_fingerprint;
    // Kept until the next attempt ends so its finish callback never deletes itself.
    std::unique_ptr<base::Process> m_lastFingerprint;
    base::Timer m_fingerprintRetry;
    std::chrono::steady_clock::time_point m_fingerprintRestsUntil;
    std::chrono::steady_clock::time_point m_fingerprintWantedUntil;
    bool m_fingerprintSpoke = false;
    base::dbus::Proxy m_logind;
    base::dbus::Match m_prepareForSleep;
    base::dbus::PendingCall m_enrollmentDevices;
    std::vector<base::dbus::PendingCall> m_enrollmentFingers;
    size_t m_enrollmentUnanswered = 0;
    bool m_enrollmentFound = false;
};
} // namespace cutefish::auth
#endif
