// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_AUTH_AUTH_CONTROLLER_H
#define CUTEFISH_AUTH_AUTH_CONTROLLER_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "auth/login_sessions.h"
#include "base/signal.h"

namespace cutefish::accounts {
class UserAccount;
}

namespace cutefish::auth {

class AuthBackend;

class AuthController
{
public:
    enum Mode {
        Login,
        Lock,
    };

    enum State {
        Idle,
        Authenticating,
        Failed,
        Succeeded,
    };

    AuthController(Mode mode, AuthBackend *backend);
    AuthController(const AuthController &) = delete;
    AuthController &operator=(const AuthController &) = delete;
    ~AuthController();

    Mode mode() const { return m_mode; }
    State state() const { return m_state; }

    std::string userName() const { return m_userName; }
    std::string displayName() const { return m_displayName; }
    std::string iconSource() const { return m_iconSource; }
    std::string errorMessage() const { return m_errorMessage; }

    bool busy() const { return m_state == Authenticating; }
    bool failed() const { return m_state == Failed; }
    bool succeeded() const { return m_state == Succeeded; }

    bool canSelectUser() const { return m_mode == Login; }

    // Lock only: off for a user who has no password, as on the live medium.
    bool passwordRequired() const { return m_mode == Login || m_passwordRequired; }
    void setPasswordRequired(bool required) { m_passwordRequired = required; }

    // Login only.
    std::vector<std::string> userNames() const;
    std::vector<std::string> userLogins() const { return m_users; }
    // Empty where an account has none.
    std::vector<std::string> userIcons() const { return m_userIcons; }
    int userIndex() const;
    void setUserIndex(int index);
    bool canPickUser() const { return m_users.size() > 1; }
    bool typesUserName() const { return m_mode == Login && m_users.empty(); }
    void setUsers(std::vector<std::string> users);

    // Login only.
    std::vector<std::string> sessionNames() const;
    int sessionIndex() const { return m_sessionIndex; }
    void setSessionIndex(int index);
    bool canSelectSession() const { return m_sessions.size() > 1; }
    void setSessions(std::vector<LoginSession> sessions);

    void setUserName(const std::string &name);

    void start();
    // Wipes the password it is given once it holds it as a Secret.
    void authenticate(std::string password);
    void unlockWithoutPassword();
    void reset();

    base::Signal<> stateChanged;
    base::Signal<> userChanged;
    base::Signal<> sessionChanged;
    base::Signal<> sessionsChanged;
    base::Signal<> usersChanged;
    base::Signal<> authenticationFailed;
    // Emitted once.
    base::Signal<> authenticationSucceeded;

private:
    void setState(State state, const std::string &errorMessage = {});
    void resolveUser();
    void selectSessionFor(const std::string &userName);
    void watchAccount(const std::string &userName);
    void accountChanged(const std::string &userName);
    std::string iconFor(const std::string &userName) const;

    Mode m_mode;
    AuthBackend *m_backend = nullptr;
    State m_state = Idle;
    std::string m_userName;
    std::string m_displayName;
    std::string m_iconSource;
    std::string m_iconStamp;
    std::string m_errorMessage;
    std::vector<std::string> m_users;
    std::vector<std::string> m_userIcons;
    std::vector<std::string> m_userIconStamps;
    std::vector<LoginSession> m_sessions;
    int m_sessionIndex = -1;
    bool m_passwordRequired = true;
    // accept() is irreversible, so it is guarded.
    bool m_accepted = false;
    std::vector<base::Subscription> m_backendSubscriptions;

    struct WatchedAccount
    {
        std::unique_ptr<accounts::UserAccount> account;
        base::Subscription changed;
    };
    std::map<std::string, WatchedAccount> m_accounts;
};

} // namespace cutefish::auth

#endif // CUTEFISH_AUTH_AUTH_CONTROLLER_H
