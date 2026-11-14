// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_H_
#define CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_H_

#include <array>
#include <memory>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "shell/controlcenter/control_center_services.h"
#include "shell/shell.h"
#include "shell/menubar/menu_bar_panels.h"

namespace cutefish::ui {
class Window;
} // namespace cutefish::ui

namespace cutefish::shell {

class ControlCenterView;
class NotificationSettings;
class PowerActions;
class PowerDialog;
class ShellWindow;
class MenuBar;
class StatusPanelView;

// The control centre and the menu bar's Wi-Fi, Sound and
// Battery panels, each a popup of the bar's window with the panels' glass,
// and the power dialog the bar's menu and org.cutefish.MenuBar ask for.
// Also tells the bar what its volume and Wi-Fi icons show.
class ControlCenter : public Component, public MenuBarPanels, public MenuBarIndicators
{
public:
    // `powerActions` may be null (no power dialog).
    ControlCenter(Shell *shell, std::unique_ptr<ControlCenterServices> services, PowerActions *powerActions);
    ~ControlCenter() override;

    // The session's, on the bar's backend.
    static std::unique_ptr<ControlCenter> create(Shell *shell, MenuBar &menuBar,
                                                 NotificationSettings *notifications);

    // --- MenuBarPanels ---
    bool hasPanel(Panel panel) const override;
    void openPanel(Panel panel, ShellWindow *bar, const base::Rect &anchor) override;
    void closePanels() override;
    bool isOpen(Panel panel) const override;

    // --- MenuBarIndicators ---
    Audio audio() const override;
    Network network() const override;

    // The bar's DeviceUsage applications for the device-users tile; call
    // deviceUsersChanged() when they change.
    void setDeviceUsersSource(std::function<std::vector<DeviceUser>()> source);
    void deviceUsersChanged();

    // The services' vpnFailed, for the bar's VpnNotifier.
    base::Signal<const std::string &, const std::string &> vpnFailed;

    // While open.
    ControlCenterView *controlCenterView() const;
    StatusPanelView *statusPanel(Panel panel) const;
    ui::Window *popup(Panel panel) const;
    ShellWindow *popupWindow(Panel panel) const;

    // Where a panel opened from `anchor` on `bar` goes, global logical.
    base::Rect panelGeometry(Panel panel, ShellWindow *bar, const base::Rect &anchor) const;

private:
    class PopupRoot;

    struct Open {
        std::unique_ptr<ui::Window> window;
        PopupRoot *root = nullptr;
        ControlCenterView *controlCenter = nullptr;
        StatusPanelView *panel = nullptr;
        ShellWindow *bar = nullptr;
        base::Rect anchor;
        base::Rect geometry;
        bool dark = false;
    };

    static size_t index(Panel panel);
    void close(Panel panel);
    void refresh();
    void retranslate();
    // A status panel's window as tall as the panel is drawn, and the glass
    // over the window.
    void updateWindow(Panel panel);
    void resizeControlCenter();
    void fitToWorkArea(Panel panel);
    void updatePower();

    Shell *m_shell;
    std::unique_ptr<ControlCenterServices> m_services;
    PowerActions *m_powerActions;
    std::unique_ptr<PowerDialog> m_powerDialog;
    std::array<Open, 4> m_open;
    Audio m_audio;
    Network m_network;
    std::vector<base::Subscription> m_subscriptions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_H_
