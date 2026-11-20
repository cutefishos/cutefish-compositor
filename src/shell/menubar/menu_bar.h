// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MENU_BAR_H_
#define CUTEFISH_SHELL_MENUBAR_MENU_BAR_H_

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "base/dbus.h"
#include "base/signal.h"
#include "base/thread_pool.h"
#include "shell/menubar/app_menu_registrar.h"
#include "shell/menubar/battery.h"
#include "shell/menubar/brightness.h"
#include "shell/menubar/gtk_settings.h"
#include "shell/menubar/mpris.h"
#include "shell/menubar/power_actions.h"
#include "shell/menubar/status_notifier_watcher.h"
#include "shell/menubar/system_tray_model.h"

namespace cutefish::powerprofiles {
class PowerProfiles;
} // namespace cutefish::powerprofiles

namespace cutefish::shell {

class TaskManager;

// Exports org.cutefish.MenuBar at /MenuBar.
class MenuBar
{
public:
    // MenuBar/ShowWiFi, ShowBattery, ShowSound and ShowBluetooth in shell.conf;
    // all but Bluetooth are shown by default.
    enum class Control {
        WiFi,
        Battery,
        Sound,
        Bluetooth,
    };

    // `tasks` may be null.
    MenuBar(base::dbus::Connection *sessionBus, base::dbus::Connection *systemBus, TaskManager *tasks,
            std::string settingsPath);
    ~MenuBar();

    MenuBar(const MenuBar &) = delete;
    MenuBar &operator=(const MenuBar &) = delete;

    // False when one of the names is taken.
    bool registerServices();

    SystemTrayModel &systemTray() { return m_tray; }
    AppMenuRegistrar &appMenuRegistrar() { return m_registrar; }
    MprisManager &mpris() { return m_mpris; }
    Battery &battery() { return *m_battery; }
    Brightness &brightness() { return m_brightness; }
    powerprofiles::PowerProfiles &powerProfiles() { return *m_powerProfiles; }
    PowerActions &powerActions() { return m_powerActions; }

    // The clock follows the primary language in the session's region, not
    // LC_TIME: the region the system was installed with would otherwise pick
    // its words.
    const std::string &timeLocale() const { return m_timeLocale; }
    const std::string &timeFormat() const { return m_timeFormat; }
    bool twentyFourTime() const { return m_twentyFourTime; }

    base::Signal<> timeFormatChanged;

    bool isControlShown(Control control) const;
    void setControlShown(Control control, bool shown);
    static std::optional<Control> controlFromName(const std::string &name);

    base::Signal<> controlsChanged;

private:
    static constexpr int kControlCount = 4;

    void reloadLanguage();
    void updateTimeFormat();

    base::dbus::Connection *m_bus;
    std::string m_settingsPath;
    StatusNotifierWatcher m_watcher;
    SystemTrayModel m_tray;
    AppMenuRegistrar m_registrar;
    GtkSettings m_gtkSettings;
    MprisManager m_mpris;
    std::unique_ptr<Battery> m_battery;
    Brightness m_brightness;
    std::unique_ptr<powerprofiles::PowerProfiles> m_powerProfiles;
    PowerActions m_powerActions;
    std::unique_ptr<base::dbus::Object> m_object;
    base::dbus::Match m_languageChanged;
    base::Job m_language;
    base::Job m_controlsLoad;
    std::array<bool, kControlCount> m_controlShown {true, true, true, false};
    std::array<bool, kControlCount> m_controlSet {};
    bool m_twentyFourTime = false;
    std::string m_timeLocale;
    std::string m_timeFormat;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MENU_BAR_H_
