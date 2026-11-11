// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "auth/auth_controller.h"
#include "auth/auth_backend.h"
#include "auth/secret.h"

#include "base/logging.h"
#include "base/strings.h"
#include "core/main_loop.h"
#include "cutefish/accounts/user_account.h"
#include <algorithm>
#include <cassert>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cutefish::auth {

namespace {

// Off by default. Nothing logged from this category ever carries a credential:
// the password never reaches a log stream, in any build.
const base::LogCategory cfAuth("cutefish.auth", base::LogLevel::Warning);

using base::strings::trimmed;

std::string accountDisplayName(const std::string &userName)
{
    if (const passwd *entry = getpwnam(userName.c_str())) {
        // GECOS is comma-separated; the first field is the full name.
        const std::string_view gecos = entry->pw_gecos ? entry->pw_gecos : "";
        const std::string full(trimmed(gecos.substr(0, gecos.find(','))));
        if (!full.empty())
            return full;
    }
    return userName;
}

std::string accountIcon(const std::string &userName)
{
    const passwd *entry = getpwnam(userName.c_str());
    const std::string home = entry && entry->pw_dir ? entry->pw_dir : "";
    // AccountsService first, then the per-home face file: the same two places
    // the rest of Cutefish looks, and neither needs a running service.
    const std::string candidates[] = {
            "/var/lib/AccountsService/icons/" + userName,
            home.empty() ? std::string() : home + "/.face",
            home.empty() ? std::string() : home + "/.face.icon",
    };
    for (const std::string &path : candidates) {
        struct stat info;
        if (!path.empty() && stat(path.c_str(), &info) == 0)
            return path;
    }
    return {};
}

// The service copies every picture it is given to the same name.
std::string pictureStamp(const std::string &path)
{
    struct stat info;
    if (path.empty() || stat(path.c_str(), &info) != 0)
        return path;
    return path + "|" + std::to_string(info.st_ino) + ":" + std::to_string(info.st_size) + ":"
            + std::to_string(info.st_mtim.tv_sec) + "." + std::to_string(info.st_mtim.tv_nsec);
}

} // namespace

AuthController::AuthController(Mode mode, AuthBackend *backend)
    : m_mode(mode)
    , m_backend(backend)
{
    assert(m_backend);
    m_backendSubscriptions.push_back(m_backend->failed.connect([this](const std::string &message) {
        if (m_state == Succeeded)
            return;
        setState(Failed, message);
        authenticationFailed();
    }));
    m_backendSubscriptions.push_back(m_backend->succeeded.connect([this] {
        // Only an outstanding attempt can authorize a transition.
        if (m_accepted || m_state != Authenticating)
            return;
        m_accepted = true;
        setState(Succeeded);
        // accept() before the signal, so a handler that tears the process down
        // cannot run before the request that unlocks the session was sent.
        m_backend->accept();
        authenticationSucceeded();
    }));
    resolveUser();
    if (m_mode == Login) {
        setUsers(findLoginUsers());
        setSessions(findLoginSessions(loginSessionDirectories()));
    }
}

std::vector<std::string> AuthController::userNames() const
{
    std::vector<std::string> names;
    for (const std::string &user : m_users)
        names.push_back(accountDisplayName(user));
    return names;
}

int AuthController::userIndex() const
{
    const auto found = std::find(m_users.begin(), m_users.end(), m_userName);
    return found == m_users.end() ? -1 : int(found - m_users.begin());
}

void AuthController::setUsers(std::vector<std::string> users)
{
    m_users = std::move(users);
    m_userIcons.clear();
    m_userIconStamps.clear();
    for (const std::string &user : m_users) {
        watchAccount(user);
        m_userIcons.push_back(iconFor(user));
        m_userIconStamps.push_back(pictureStamp(m_userIcons.back()));
    }
    usersChanged();
    if (m_users.empty() || userIndex() >= 0) {
        userChanged();
        return;
    }
    // Whoever logged in last, else the first account: nobody types a name
    // the system already knows.
    const std::string last = m_backend->defaultUserName();
    const bool listed = std::find(m_users.begin(), m_users.end(), last) != m_users.end();
    setUserName(listed ? last : m_users.front());
}

void AuthController::setUserIndex(int index)
{
    if (index >= 0 && index < int(m_users.size()))
        setUserName(m_users[size_t(index)]);
}

AuthController::~AuthController() = default;

std::vector<std::string> AuthController::sessionNames() const
{
    std::vector<std::string> names;
    for (const LoginSession &session : m_sessions)
        names.push_back(session.name);
    return names;
}

void AuthController::setSessions(std::vector<LoginSession> sessions)
{
    m_sessions = std::move(sessions);
    m_sessionIndex = -1;
    sessionsChanged();
    selectSessionFor(m_userName);
}

void AuthController::setSessionIndex(int index)
{
    if (index < 0 || index >= int(m_sessions.size()) || index == m_sessionIndex || busy()
        || succeeded())
        return;
    m_sessionIndex = index;
    sessionChanged();
}

void AuthController::selectSessionFor(const std::string &userName)
{
    if (m_sessions.empty())
        return;
    // The user's last session, else Cutefish, else the first there is.
    const std::string remembered = userName.empty() ? std::string() : lastLoginSession(userName);
    int index = -1;
    for (const std::string &id : {remembered, std::string("cutefish-wayland")}) {
        for (int i = 0; index < 0 && !id.empty() && i < int(m_sessions.size()); ++i) {
            if (m_sessions[size_t(i)].id == id)
                index = i;
        }
    }
    index = std::max(index, 0);
    if (index == m_sessionIndex)
        return;
    m_sessionIndex = index;
    sessionChanged();
}

void AuthController::resolveUser()
{
    m_userName = m_backend->defaultUserName();
    if (m_userName.empty() && m_mode == Lock) {
        if (const passwd *entry = getpwuid(getuid()))
            m_userName = entry->pw_name ? entry->pw_name : "";
    }
    m_displayName = m_userName.empty() ? std::string() : accountDisplayName(m_userName);
    watchAccount(m_userName);
    m_iconSource = m_userName.empty() ? std::string() : iconFor(m_userName);
    m_iconStamp = pictureStamp(m_iconSource);
    userChanged();
}

void AuthController::setUserName(const std::string &name)
{
    if (!canSelectUser() || busy() || succeeded())
        return;
    m_userName = std::string(trimmed(name));
    m_displayName = accountDisplayName(m_userName);
    watchAccount(m_userName);
    m_iconSource = iconFor(m_userName);
    m_iconStamp = pictureStamp(m_iconSource);
    userChanged();
    selectSessionFor(m_userName);
}

void AuthController::watchAccount(const std::string &userName)
{
    if (userName.empty() || m_accounts.contains(userName))
        return;
    const passwd *entry = getpwnam(userName.c_str());
    base::dbus::Connection *bus = entry ? base::dbus::Connection::system(core::mainLoop()) : nullptr;
    if (!bus)
        return;
    WatchedAccount &watched = m_accounts[userName];
    watched.account = std::make_unique<accounts::UserAccount>(int64_t(entry->pw_uid), bus);
    watched.changed = watched.account->changed.connect([this, userName] { accountChanged(userName); });
}

void AuthController::accountChanged(const std::string &userName)
{
    const std::string icon = iconFor(userName);
    const std::string stamp = pictureStamp(icon);
    if (userName == m_userName && stamp != m_iconStamp) {
        m_iconSource = icon;
        m_iconStamp = stamp;
        userChanged();
    }
    const auto listed = std::find(m_users.begin(), m_users.end(), userName);
    if (listed == m_users.end())
        return;
    const size_t index = size_t(listed - m_users.begin());
    if (stamp == m_userIconStamps[index])
        return;
    m_userIcons[index] = icon;
    m_userIconStamps[index] = stamp;
    usersChanged();
}

// The service's choice once it has answered; until then, or without it, the
// files it and the account keep.
std::string AuthController::iconFor(const std::string &userName) const
{
    const auto found = m_accounts.find(userName);
    if (found != m_accounts.end()) {
        std::string icon = found->second.account->iconFileName();
        if (!icon.empty())
            return icon;
    }
    return accountIcon(userName);
}

void AuthController::start()
{
    m_backend->start();
}

void AuthController::authenticate(std::string password)
{
    Secret secret(password);
    wipeString(password);
    if (m_state == Authenticating || m_state == Succeeded)
        return;
    setState(Authenticating);
    if (m_sessionIndex >= 0 && m_sessionIndex < int(m_sessions.size()))
        m_backend->setSession(m_sessions[size_t(m_sessionIndex)]);
    m_backend->authenticate(m_userName, std::move(secret));
}

void AuthController::reset()
{
    // A success is not something the user can type their way out of.
    if (m_state == Succeeded || m_state == Authenticating)
        return;
    m_backend->reset();
    setState(Idle);
}

void AuthController::setState(State state, const std::string &errorMessage)
{
    if (m_state == state && m_errorMessage == errorMessage)
        return;
    m_state = state;
    m_errorMessage = errorMessage;
    CF_DEBUG(cfAuth) << "state" << state;
    stateChanged();
}

} // namespace cutefish::auth
