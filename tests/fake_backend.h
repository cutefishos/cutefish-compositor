// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_TESTS_FAKE_BACKEND_H
#define CUTEFISH_TESTS_FAKE_BACKEND_H

#include "auth/auth_backend.h"

#include <string>

class FakeBackend : public cutefish::auth::AuthBackend
{

public:
    std::string defaultUserName() const override { return "tester"; }

    void start() override { ++startCount; }

    void authenticate(const std::string &userName, cutefish::auth::Secret secret) override
    {
        lastUser = userName;
        lastSecret = std::string(secret.constData(), secret.size());
        ++authenticateCount;
    }

    void reset() override { ++resetCount; }

    void accept() override { ++acceptCount; }

    void setSession(const cutefish::auth::LoginSession &session) override { lastSession = session.id; }

    bool fingerprintAvailable(const std::string &) const override { return hasReader; }

    void startFingerprint(const std::string &) override
    {
        ++fingerprintStartCount;
        readerRunning = true;
    }

    void stopFingerprint() override
    {
        ++fingerprintStopCount;
        readerRunning = false;
    }

    bool fingerprintEnrolled() const override { return enrolled; }

    void checkFingerprintEnrollment(const std::string &) override { ++enrollmentCheckCount; }

    void setEnrolled(bool value)
    {
        enrolled = value;
        fingerprintEnrollmentChanged();
    }

    void mismatch()
    {
        readerRunning = false;
        fingerprintNoMatch();
    }

    void succeed() { succeeded(); }

    void reject(const std::string &message = "Incorrect password") { failed(message); }

    void touch()
    {
        readerRunning = false;
        fingerprintSucceeded();
    }

    void wake()
    {
        stopFingerprint();
        systemWoke();
    }

    bool hasReader = false;
    bool enrolled = false;
    bool readerRunning = false;
    int enrollmentCheckCount = 0;
    int fingerprintStartCount = 0;
    int fingerprintStopCount = 0;
    int startCount = 0;
    int authenticateCount = 0;
    int resetCount = 0;
    int acceptCount = 0;
    std::string lastUser;
    std::string lastSecret;
    std::string lastSession;
};

#endif // CUTEFISH_TESTS_FAKE_BACKEND_H
