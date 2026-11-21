// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_APP_LAUNCHER_H_
#define CUTEFISH_SHELL_COMMON_APP_LAUNCHER_H_

#include <functional>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "shell/common/pending_calls.h"

namespace cutefish::base {
struct DesktopEntry;
} // namespace cutefish::base

namespace cutefish::shell {

// Starts applications through org.cutefish.AppRuntime, or directly when it does
// not answer.
class ApplicationLauncher
{
public:
    using Done = std::function<void(bool started)>;
    using TokenSource = std::function<std::string()>;

    // `createToken`, when given, makes the activation token each start carries.
    explicit ApplicationLauncher(base::dbus::Connection *bus = base::dbus::Connection::session(),
                                 TokenSource createToken = {});

    void launch(const base::DesktopEntry &entry, const std::vector<std::string> &arguments = {},
                Done done = {});
    void launchCommand(const std::vector<std::string> &command, const std::string &workingDirectory,
                       const std::string &appId, Done done = {});
    void quitApplication(const std::string &appId);
    // For a bundle while nothing runs bundles: the file manager offers to
    // install what does.
    static void openInFileManager(const std::string &path);
    // `environment` holds `NAME=value` entries set over the compositor's.
    static bool startDetached(const std::string &program, const std::vector<std::string> &arguments = {},
                              const std::vector<std::string> &environment = {});

private:
    std::string newToken() const;

    base::dbus::Proxy m_runtime;
    TokenSource m_createToken;
    PendingCalls m_calls;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_APP_LAUNCHER_H_
