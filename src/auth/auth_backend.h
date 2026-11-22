// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_AUTH_AUTH_BACKEND_H
#define CUTEFISH_AUTH_AUTH_BACKEND_H

#include "auth/login_sessions.h"
#include "auth/secret.h"
#include "base/signal.h"

#include <string>

namespace cutefish::auth {

class AuthBackend
{
public:
    AuthBackend() = default;
    AuthBackend(const AuthBackend &) = delete;
    AuthBackend &operator=(const AuthBackend &) = delete;
    virtual ~AuthBackend() = default;

    // Empty means the UI should ask.
    virtual std::string defaultUserName() const = 0;

    // Called once, after the UI exists, so a prompt or an early error has somewhere
    // to go.
    virtual void start() = 0;

    // The secret dies with the backend's conversation.
    virtual void authenticate(const std::string &userName, Secret secret) = 0;

    // Must leave no credential behind.
    virtual void reset() = 0;

    virtual void accept() = 0;

    // Chosen before authenticate(); only a login backend uses it.
    virtual void setSession(const LoginSession &) { }

    // Fingerprint verification runs beside the password path, not in front of
    // it, so a typed password never waits for the reader. A backend without a
    // reader leaves these alone. startFingerprint() may decline: the reader
    // is not kept verifying, and rests after an attempt ends.
    virtual bool fingerprintAvailable(const std::string &) const { return false; }
    virtual void startFingerprint(const std::string &) { }
    virtual void stopFingerprint() { }

    // Whether the user has a finger enrolled on a reader that is present, as
    // last found; checkFingerprintEnrollment() asks again and reports a change
    // through fingerprintEnrollmentChanged.
    virtual bool fingerprintEnrolled() const { return false; }
    virtual void checkFingerprintEnrollment(const std::string &) { }

    // accept() may now be called, exactly once.
    base::Signal<> succeeded;

    // The reader verified the user while no password was being checked.
    base::Signal<> fingerprintSucceeded;

    // The reader read a finger that is not the user's.
    base::Signal<> fingerprintNoMatch;

    base::Signal<> fingerprintEnrollmentChanged;

    // The machine woke from sleep, which resets the reader; whoever woke it
    // is at the screen.
    base::Signal<> systemWoke;

    // `message` never contains the credential.
    base::Signal<const std::string &> failed;
};

} // namespace cutefish::auth

#endif // CUTEFISH_AUTH_AUTH_BACKEND_H
