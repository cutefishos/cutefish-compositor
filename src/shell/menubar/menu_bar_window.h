// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MENU_BAR_WINDOW_H_
#define CUTEFISH_SHELL_MENUBAR_MENU_BAR_WINDOW_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "shell/shell.h"
#include "shell/menubar/bar_item.h"
#include "shell/menubar/menu_bar.h"
#include "shell/menubar/menu_bar_app_menus.h"
#include "shell/menubar/menu_bar_panels.h"

namespace cutefish::ui {
class Menu;
}

namespace cutefish::shell {

class ClockItem;
class DeviceUsage;
class VpnNotifier;
class ShellWindow;
class MenuBarBackdrop;
class StatusIcon;
class StatusItemsView;
class StopItem;
class SystemTrayView;
class Wallpaper;

class MenuBarWindow : public Component, public BarContext
{
public:
    static constexpr int kHeight = 32;

    MenuBarWindow(Shell *shell, std::unique_ptr<MenuBar> menuBar);
    ~MenuBarWindow() override;

    MenuBar &menuBar() { return *m_menuBar; }
    MenuBarBackdrop &backdrop() { return *m_backdrop; }
    // Null leaves the bar's contents to the theme.
    void setWallpaper(Wallpaper *wallpaper);
    DeviceUsage *deviceUsage() const { return m_deviceUsage.get(); }
    void setDeviceUsage(std::unique_ptr<DeviceUsage> usage);
    VpnNotifier &vpnNotifier() { return *m_vpnNotifier; }
    ShellWindow *window() const { return m_window.get(); }

    // A null setAppMenus() takes the app menus out, before their component
    // goes.
    MenuBarAppMenusHost appMenusHost();
    void setAppMenus(std::unique_ptr<MenuBarAppMenus> appMenus);

    void addPanels(MenuBarPanels *panels);
    void removePanels(MenuBarPanels *panels);
    void setIndicators(MenuBarIndicators *indicators);
    void togglePanel(MenuBarPanels::Panel panel);
    void closePanels();
    void closePopups();

    void setRecording(bool recording);
    std::function<void()> onStopRecording;
    void setAgentControl(bool active);
    std::function<void()> onStopAgentControl;

    SystemTrayView *tray() const { return m_tray; }
    StatusItemsView *statusItems() const { return m_statusItems; }
    StatusIcon *volumeIcon() const { return m_volume; }
    StatusIcon *bluetoothIcon() const { return m_bluetooth; }
    StatusIcon *networkIcon() const { return m_network; }
    StatusIcon *batteryIcon() const { return m_battery; }
    StatusIcon *controlIcon() const { return m_control; }
    ClockItem *clock() const { return m_clock; }
    ui::Menu *controlMenu() const { return m_controlMenu.get(); }
    bool lightContents() const override;
    ui::Color fill() const { return m_fill; }
    bool panelOpen() const;
    void platePainted() override;
    void updateClock();

private:
    class Root;

    void updateGeometry();
    void updateLook();
    void updateIndicators();
    void updateBattery();
    void layoutRow();
    void armClock();
    void toggleControlMenu(StatusIcon *icon, MenuBar::Control control);
    void closeControlMenu();
    MenuBarPanels *panelsFor(MenuBarPanels::Panel panel) const;
    BarItem *itemFor(MenuBarPanels::Panel panel) const;
    void updateChecked();
    void updatePlateBlur();
    void updateStatusItemLimits();
    ui::RectF availableGeometry() const;

    Shell *m_shell;
    std::unique_ptr<MenuBar> m_menuBar;
    std::unique_ptr<MenuBarBackdrop> m_backdrop;
    std::unique_ptr<DeviceUsage> m_deviceUsage;
    std::unique_ptr<VpnNotifier> m_vpnNotifier;
    base::Subscription m_deviceUsageChanged;
    std::unique_ptr<ShellWindow> m_window;
    Root *m_root = nullptr;
    ui::View *m_menuSlot = nullptr;
    MenuBarAppMenus *m_appMenus = nullptr;
    StopItem *m_recording = nullptr;
    StopItem *m_agentControl = nullptr;
    StatusItemsView *m_statusItems = nullptr;
    SystemTrayView *m_tray = nullptr;
    StatusIcon *m_volume = nullptr;
    StatusIcon *m_bluetooth = nullptr;
    StatusIcon *m_network = nullptr;
    StatusIcon *m_battery = nullptr;
    StatusIcon *m_control = nullptr;
    ClockItem *m_clock = nullptr;
    std::unique_ptr<ui::Menu> m_controlMenu;
    StatusIcon *m_controlMenuIcon = nullptr;
    std::vector<MenuBarPanels *> m_panels;
    std::vector<base::Subscription> m_panelSubscriptions;
    MenuBarIndicators *m_indicators = nullptr;
    base::Subscription m_indicatorsChanged;
    // A wall-clock timerfd: it fires on the minute (or second) the clock
    // shows next, after a suspend, and whenever the system time is set.
    int m_clockFd = -1;
    int m_clockWatch = 0;
    bool m_lightContents = false;
    ui::Color m_fill {0, 0, 0, 0};
    // In window coordinates; empty for none.
    ui::RectF m_blurPlate;
    std::vector<base::Subscription> m_subscriptions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MENU_BAR_WINDOW_H_
