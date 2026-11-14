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
class MenuBar;
class MenuBarBackdrop;
class StatusIcon;
class StatusItemsView;
class StopItem;
class SystemTrayView;
class Wallpaper;

// The menu bar: a 32 px strip along the top of the primary output, reserved
// from the work area, with no background of its own. Left to right: the menu
// bar, the stop buttons of a recording and of an agent's control, the items
// applications draw, the tray, the volume, Wi-Fi, battery and control centre
// icons, and the clock. Its contents are light or dark by what the wallpaper
// shows behind it.
class MenuBarWindow : public Component, public BarContext
{
public:
    static constexpr int kHeight = 32;

    // `menuBar` is the backend: tray, registrar, battery, power actions,
    // the clock's format and org.cutefish.MenuBar.
    MenuBarWindow(Shell *shell, std::unique_ptr<MenuBar> menuBar);
    ~MenuBarWindow() override;

    MenuBar &menuBar() { return *m_menuBar; }
    MenuBarBackdrop &backdrop() { return *m_backdrop; }
    // What the desktop shows decides the bar's contents; null leaves them to
    // the theme.
    void setWallpaper(Wallpaper *wallpaper);
    // Which applications use a camera, a microphone or record the system's
    // audio: the control icon's dot, and the control centre's tile.
    DeviceUsage *deviceUsage() const { return m_deviceUsage.get(); }
    // Tests give their own; the session's is made with the bar on a bus.
    void setDeviceUsage(std::unique_ptr<DeviceUsage> usage);
    // Tells a VPN that went down on its own (NetworkManager's
    // Handler::vpnFailed, from the control centre's client).
    VpnNotifier &vpnNotifier() { return *m_vpnNotifier; }
    ShellWindow *window() const { return m_window.get(); }

    // --- The app menus, the panels and the indicators of other components ---
    // The app menus go in the first slot; null takes them out (before their
    // component goes).
    MenuBarAppMenusHost appMenusHost();
    void setAppMenus(std::unique_ptr<MenuBarAppMenus> appMenus);

    void addPanels(MenuBarPanels *panels);
    void removePanels(MenuBarPanels *panels);
    void setIndicators(MenuBarIndicators *indicators);
    // An item's click: every popup closes, then the panel opens unless it
    // was the one open.
    void togglePanel(MenuBarPanels::Panel panel);
    // Closes the panels and every other popup of the bar.
    void closePanels();
    void closePopups();

    // The stop buttons, shown while a recording or an agent's control runs.
    void setRecording(bool recording);
    std::function<void()> onStopRecording;
    void setAgentControl(bool active);
    std::function<void()> onStopAgentControl;

    // --- For tests and the reference render ---
    SystemTrayView *tray() const { return m_tray; }
    StatusItemsView *statusItems() const { return m_statusItems; }
    StatusIcon *volumeIcon() const { return m_volume; }
    StatusIcon *networkIcon() const { return m_network; }
    StatusIcon *batteryIcon() const { return m_battery; }
    StatusIcon *controlIcon() const { return m_control; }
    ClockItem *clock() const { return m_clock; }
    // The bar's contents are light.
    bool lightContents() const override;
    // One of the panels is open.
    bool panelOpen() const;
    void platePainted() override;
    // Reads the clock now.
    void updateClock();

private:
    class Root;

    void updateGeometry();
    void updateLook();
    void updateIndicators();
    void updateBattery();
    void layoutRow();
    void armClock();
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
    // The slot's placeholder, or the app menus.
    ui::View *m_menuSlot = nullptr;
    MenuBarAppMenus *m_appMenus = nullptr;
    StopItem *m_recording = nullptr;
    StopItem *m_agentControl = nullptr;
    StatusItemsView *m_statusItems = nullptr;
    SystemTrayView *m_tray = nullptr;
    StatusIcon *m_volume = nullptr;
    StatusIcon *m_network = nullptr;
    StatusIcon *m_battery = nullptr;
    StatusIcon *m_control = nullptr;
    ClockItem *m_clock = nullptr;
    std::vector<MenuBarPanels *> m_panels;
    std::vector<base::Subscription> m_panelSubscriptions;
    MenuBarIndicators *m_indicators = nullptr;
    base::Subscription m_indicatorsChanged;
    // A wall-clock timerfd: it fires on the minute (or second) the clock
    // shows next, after a suspend, and whenever the system time is set.
    int m_clockFd = -1;
    int m_clockWatch = 0;
    bool m_lightContents = false;
    // The plate the blur is behind, in window coordinates; empty for none.
    ui::RectF m_blurPlate;
    std::vector<base::Subscription> m_subscriptions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MENU_BAR_WINDOW_H_
