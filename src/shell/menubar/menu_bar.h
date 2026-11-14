// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MENU_BAR_H_
#define CUTEFISH_SHELL_MENUBAR_MENU_BAR_H_

#include <memory>
#include <string>

#include "base/dbus.h"
#include "base/signal.h"
#include "base/thread_pool.h"
#include "shell/menubar/app_menu_registrar.h"
#include "shell/menubar/battery.h"
#include "shell/menubar/brightness.h"
#include "shell/menubar/mpris.h"
#include "shell/menubar/power_actions.h"
#include "shell/menubar/status_notifier_watcher.h"
#include "shell/menubar/system_tray_model.h"

namespace cutefish::powerprofiles {
class PowerProfiles;
} // namespace cutefish::powerprofiles

namespace cutefish::shell {

class TaskManager;

// The menu bar without its views: the tray and its watcher, the AppMenu
// registrar, the media player, battery, brightness and power profiles, the
// power actions, the clock's format, and org.cutefish.MenuBar at
// /MenuBar.
class MenuBar
{
public:
    // `settingsPath` is shell.conf; `tasks` may be null.
    MenuBar(base::dbus::Connection *sessionBus, base::dbus::Connection *systemBus, TaskManager *tasks,
            std::string settingsPath);
    ~MenuBar();

    MenuBar(const MenuBar &) = delete;
    MenuBar &operator=(const MenuBar &) = delete;

    // org.cutefish.MenuBar, org.kde.StatusNotifierWatcher and
    // com.canonical.AppMenu.Registrar; false when one is taken.
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
    // A format string for base::Locale(timeLocale()).toString().
    const std::string &timeFormat() const { return m_timeFormat; }
    // The session's 24-hour choice, else the region's clock.
    bool twentyFourTime() const { return m_twentyFourTime; }

    base::Signal<> timeFormatChanged;

private:
    void reloadLanguage();
    void updateTimeFormat();

    base::dbus::Connection *m_bus;
    std::string m_settingsPath;
    StatusNotifierWatcher m_watcher;
    SystemTrayModel m_tray;
    AppMenuRegistrar m_registrar;
    MprisManager m_mpris;
    std::unique_ptr<Battery> m_battery;
    Brightness m_brightness;
    std::unique_ptr<powerprofiles::PowerProfiles> m_powerProfiles;
    PowerActions m_powerActions;
    std::unique_ptr<base::dbus::Object> m_object;
    base::dbus::Match m_languageChanged;
    base::Job m_language;
    bool m_twentyFourTime = false;
    std::string m_timeLocale;
    std::string m_timeFormat;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MENU_BAR_H_
