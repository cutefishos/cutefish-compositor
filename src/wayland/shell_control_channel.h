// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WAYLAND_SHELL_CONTROL_CHANNEL_H_
#define CUTEFISH_WAYLAND_SHELL_CONTROL_CHANNEL_H_

#include <functional>
#include <string>

namespace cutefish::base {
class EventLoop;
}

namespace cutefish::wayland {

// A SOCK_STREAM UNIX socket over which the session passes, by SCM_RIGHTS, one end of each
// socketpair it gives a privileged client; it carries file descriptors and nothing else.
class ShellControlChannel
{
public:
    // Watches its sockets on `loop`, the main loop when null.
    explicit ShellControlChannel(base::EventLoop *loop = nullptr);
    ShellControlChannel(const ShellControlChannel &) = delete;
    ShellControlChannel &operator=(const ShellControlChannel &) = delete;
    ~ShellControlChannel();

    // Removes a stale socket left by a previous compositor. Returns false and logs on failure.
    bool listen(const std::string &path);

    // The receiver takes ownership of `fd`.
    std::function<void(int fd)> connectionReceived;

private:
    void onAcceptReady();
    void onControlReady();
    void closeControl();

    base::EventLoop &m_loop;
    int m_listenFd = -1;
    // One session at a time. A second connection replaces the first, which is
    // what a session restart looks like from here.
    int m_controlFd = -1;
    int m_listenWatch = 0;
    int m_controlWatch = 0;
    std::string m_path;
};

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_SHELL_CONTROL_CHANNEL_H_
