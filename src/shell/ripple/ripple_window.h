// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_RIPPLE_RIPPLE_WINDOW_H_
#define CUTEFISH_SHELL_RIPPLE_RIPPLE_WINDOW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/geometry.h"
#include "base/timer.h"
#include "shell/ripple/ripple_client.h"
#include "shell/ripple/ripple_view.h"
#include "shell/shell.h"

namespace cutefish::shell {

class ShellWindow;

// Ripple's panel: what is nearby to send to, an offer to answer, and the
// transfer on its way. A Picker window, so it only borrows the keyboard.
class RippleWindow : public Component
{
public:
    RippleWindow(Shell *shell, base::dbus::Connection *bus);
    ~RippleWindow() override;

    bool isOpen() const;
    void open();
    void close();
    void toggle();

    // Files handed in by the file manager: show them, and the devices to send
    // them to. The argument is a JSON array of paths.
    void share(const std::string &pathsJson);

    // org.cutefish.RipplePanel: the way in for "Send with Ripple".
    bool registerService();

private:
    base::Rect geometry() const;
    void place();
    void applyGlass();
    void refreshPeers();
    void sendTo(int index);
    void acceptOffer();
    void declineOffer();

    Shell *m_shell;
    base::dbus::Connection *m_bus = nullptr;
    std::unique_ptr<RippleClient> m_client;
    std::unique_ptr<ShellWindow> m_window;
    RippleView *m_view = nullptr;
    std::unique_ptr<base::dbus::Object> m_object;
    base::Timer m_closeTimer;
    std::vector<base::Subscription> m_subscriptions;
    std::vector<RipplePeer> m_peers;
    std::vector<std::string> m_pending;
    RippleOffer m_offer;
    std::string m_session;
    std::string m_sendingTo;
    std::string m_doneFolder;
    int m_glassHeight = -1;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_RIPPLE_RIPPLE_WINDOW_H_
