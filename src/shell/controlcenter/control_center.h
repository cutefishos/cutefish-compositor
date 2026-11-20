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

class ControlCenterPager;
class ControlCenterView;
class NotificationSettings;
class PowerActions;
class PowerDialog;
class ShellWindow;
class MenuBar;
class StatusPanelView;

class ControlCenter : public Component, public MenuBarPanels, public MenuBarIndicators
{
public:
    // `powerActions` may be null (no power dialog).
    ControlCenter(Shell *shell, std::unique_ptr<ControlCenterServices> services, PowerActions *powerActions);
    ~ControlCenter() override;

    static std::unique_ptr<ControlCenter> create(Shell *shell, MenuBar &menuBar,
                                                 NotificationSettings *notifications);

    bool hasPanel(Panel panel) const override;
    void openPanel(Panel panel, ShellWindow *bar, const base::Rect &anchor) override;
    void closePanels() override;
    bool isOpen(Panel panel) const override;

    Audio audio() const override;
    Network network() const override;
    Bluetooth bluetooth() const override;

    // Call deviceUsersChanged() when the source's applications change.
    void setDeviceUsersSource(std::function<std::vector<DeviceUser>()> source);
    void deviceUsersChanged();

    base::Signal<const std::string &, const std::string &> vpnFailed;

    // Null while closed.
    ControlCenterView *controlCenterView() const;
    ControlCenterPager *controlCenterPager() const;
    StatusPanelView *statusPanel(Panel panel) const;
    ui::Window *popup(Panel panel) const;
    ShellWindow *popupWindow(Panel panel) const;

    // Global logical coordinates.
    base::Rect panelGeometry(Panel panel, ShellWindow *bar, const base::Rect &anchor) const;

private:
    class PopupRoot;

    struct Open {
        std::unique_ptr<ui::Window> window;
        PopupRoot *root = nullptr;
        ControlCenterView *controlCenter = nullptr;
        ControlCenterPager *pager = nullptr;
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
    void updateWindow(Panel panel);
    void resizeControlCenter();
    void showDetail(Panel panel);
    void escape(Panel panel);
    double detailMaximumHeight() const;
    void fitToWorkArea(Panel panel);
    void updatePower();

    Shell *m_shell;
    std::unique_ptr<ControlCenterServices> m_services;
    PowerActions *m_powerActions;
    std::unique_ptr<PowerDialog> m_powerDialog;
    std::array<Open, 5> m_open;
    Audio m_audio;
    Network m_network;
    Bluetooth m_bluetooth;
    std::vector<base::Subscription> m_subscriptions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_H_
