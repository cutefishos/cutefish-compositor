// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WAYLAND_SECURITY_DELEGATE_H_
#define CUTEFISH_WAYLAND_SECURITY_DELEGATE_H_

#include <cstdint>
#include <string>
#include <vector>

struct wl_client;

namespace cutefish::wayland {

class SecurityDelegate
{
public:
    virtual ~SecurityDelegate() = default;

    virtual bool canManageClipboard(const wl_client *client) const = 0;
    virtual bool canCaptureOutputs(const wl_client *client) const = 0;
    // Whether the process's toplevels are the session's own dialogs, stacked
    // above every application window.
    virtual bool showsSystemDialogs(int64_t pid) const = 0;
    // The computer-use service, which may ask the user to let an agent drive
    // the seat, and reads the screen while it does.
    virtual bool canControlComputer(const wl_client *) const { return false; }
    virtual bool canPlaceWindows(const wl_client *) const { return false; }
    // Their virtual keyboards type into whichever window holds the keyboard.
    virtual bool canSendKeys(const wl_client *) const { return false; }
};

// The pid comes from the kernel through the socket, so a client cannot claim an identity it does not have.
class ExecutableSecurityDelegate final : public SecurityDelegate
{
public:
    // Paths are canonicalized once; entries that do not resolve are dropped, so
    // a client can never match an empty path.
    ExecutableSecurityDelegate(const std::vector<std::string> &clipboardClients,
                               const std::vector<std::string> &captureClients,
                               const std::vector<std::string> &systemDialogClients,
                               bool captureEnabled,
                               const std::vector<std::string> &computerControlClients = {},
                               const std::vector<std::string> &windowPlacementClients = {});

    bool canManageClipboard(const wl_client *client) const override;
    bool canCaptureOutputs(const wl_client *client) const override;
    bool showsSystemDialogs(int64_t pid) const override;
    bool canControlComputer(const wl_client *client) const override;
    bool canPlaceWindows(const wl_client *client) const override;
    // The same programs as place their windows.
    bool canSendKeys(const wl_client *client) const override;
    // They get Command as Ctrl, never a terminal's rewritten shortcuts.
    bool bindsCommandKey(int64_t pid) const;

private:
    const std::vector<std::string> m_clipboardClients;
    const std::vector<std::string> m_captureClients;
    const std::vector<std::string> m_systemDialogClients;
    const std::vector<std::string> m_computerControlClients;
    const std::vector<std::string> m_windowPlacementClients;
    // Capture has nothing to read without a display, and computer control
    // nothing to show.
    const bool m_captureEnabled;
};

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_SECURITY_DELEGATE_H_
