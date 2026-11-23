// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_AUTH_LOGIN_SESSIONS_H
#define CUTEFISH_AUTH_LOGIN_SESSIONS_H

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cutefish::auth {

// Read as cutefish-dm reads wayland-sessions entries, so both agree on what exists.
struct LoginSession {
    std::string id;
    std::string name;
    std::vector<std::string> command;
    // What pam_systemd and the session need for a daemon that, like greetd,
    // sets none of it; cutefish-dm sets its own from the id.
    std::vector<std::string> environment;
};

std::vector<LoginSession> findLoginSessions(const std::vector<std::string> &directories);

std::vector<std::string> loginSessionDirectories();
// Empty restores them.
void setLoginSessionDirectoriesForTesting(std::vector<std::string> directories);

// UIDs within login.defs' UID_MIN..UID_MAX that have a login shell, in UID order.
std::vector<std::string> findLoginUsers();

// Empty under any daemon other than cutefish-dm.
std::string lastLoginUser();
std::string lastLoginSession(const std::string &user);

// A user without one shows the default.
std::string loginBackgroundFile(const std::string &user);
// Empty restores the installed directory.
void setLoginBackgroundDirectoryForTesting(std::string directory);

// The user's saved locale variables from cutefish-dm, or unset when none is stored.
std::optional<std::map<std::string, std::string>> loginLanguageEnvironment(const std::string &user);
// The current process's locale variables, for restoring the system default.
std::map<std::string, std::string> currentLanguageEnvironment();

// Splits a command line on white space, with double quotes around an
// argument holding spaces and three in a row standing for one.
std::vector<std::string> splitCommand(std::string_view command);

} // namespace cutefish::auth

#endif // CUTEFISH_AUTH_LOGIN_SESSIONS_H
