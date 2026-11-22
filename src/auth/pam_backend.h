// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_AUTH_PAM_BACKEND_H
#define CUTEFISH_AUTH_PAM_BACKEND_H
#include "auth/auth_backend.h"
#include "base/process.h"
#include "base/timer.h"
#include <memory>

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

    void accept() override { }

private:
    std::unique_ptr<base::Process> m_process;
    base::Timer m_timeout;
    bool m_pending = false;
    // Kept until the next attempt so its finish callback never deletes itself.
    std::unique_ptr<base::Process> m_fingerprint;
    bool m_fingerprintDone = true;
    bool m_fingerprintActive = false;
    void setFingerprintActive(bool active);
};
} // namespace cutefish::auth
#endif
