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

    // accept() may now be called, exactly once.
    base::Signal<> succeeded;

    // `message` never contains the credential.
    base::Signal<const std::string &> failed;
};

} // namespace cutefish::auth

#endif // CUTEFISH_AUTH_AUTH_BACKEND_H
