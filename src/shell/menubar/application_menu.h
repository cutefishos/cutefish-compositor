// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_APPLICATION_MENU_H_
#define CUTEFISH_SHELL_MENUBAR_APPLICATION_MENU_H_

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "base/timer.h"

namespace cutefish::compositor {
class MenuModel;
}

namespace cutefish::shell {

class AppMenuRegistrar;
class DBusMenu;
class TaskManager;
class TaskWindows;

// Which application the menu bar belongs to and its menu: the menu bar's
// non-visual half. The bar shows the user's menu, then title() and the
// entries of menu(); the user's menu, and Hide, Hide Others and Quit for an
// application without a menu of its own role, are the view's.
//
// A window's main menu (cutefish_main_menu_v1) is ready as soon as it is
// set; a com.canonical.dbusmenu export is imported.
//
// Menus are imported as soon as a window or its exporter appears, so that
// focusing it later finds the menu ready and the bar switches in one step.
// A switch to an application whose menu is still on its way keeps the
// previous bar until it arrives or 250 ms pass. Every input is handled on the
// next turn of the loop, so that a burst becomes one switch.
class ApplicationMenu
{
public:
    static constexpr std::chrono::milliseconds kPendingMenuTimeout{250};

    ApplicationMenu(base::dbus::Connection *bus, TaskWindows &windows, TaskManager &tasks,
                    AppMenuRegistrar *registrar);
    ~ApplicationMenu();

    ApplicationMenu(const ApplicationMenu &) = delete;
    ApplicationMenu &operator=(const ApplicationMenu &) = delete;

    // "Desktop" on the desktop, the active application's name (its entry's,
    // else its window title), or empty when the bar shows the user's menu
    // alone.
    const std::string &title() const { return m_title; }
    // The application's menu, ready to show; null for none.
    compositor::MenuModel *menu() const { return m_menu; }

    // Whether the desktop has the focus, which has no application to quit.
    void setDesktop(bool desktop);
    // Names the desktop again after the session language changed.
    void retranslate();

    // Quit, Hide and Hide Others of the application the bar names.
    void quitApplication();
    void hideApplication();
    void hideOtherApplications();

    // The bar now belongs to another application, or none: an open menu of
    // the previous one has to go.
    base::Signal<> switched;
    // Only the name changed.
    base::Signal<> titleChanged;
    // The application asks for an entry of its menu to be shown.
    base::Signal<int32_t> activationRequested;

private:
    struct Import {
        std::unique_ptr<DBusMenu> menu;
        bool ready = false;
        std::vector<base::Subscription> subscriptions;
    };

    static std::string importKey(const std::string &service, const std::string &path);
    Import *importFor(const std::string &service, const std::string &path);
    void menuAddressOf(uint64_t window, std::string *service, std::string *path) const;
    void prefetch(uint64_t window);
    void onImported(const std::string &key, int32_t id);
    void dropService(const std::string &service);
    void pruneImports();
    void scheduleUpdate();
    void update();
    void commit(const std::string &title, compositor::MenuModel *menu,
                std::shared_ptr<compositor::MenuModel> held = nullptr);
    void commitPending();
    std::string applicationTitle() const;

    base::dbus::Connection *m_bus;
    TaskWindows &m_windows;
    TaskManager &m_tasks;
    AppMenuRegistrar *m_registrar;
    bool m_desktop = false;
    std::string m_title;
    compositor::MenuModel *m_menu = nullptr;
    // A main menu stays while shown, even after its client let it go.
    std::shared_ptr<compositor::MenuModel> m_heldMenu;
    bool m_shown = false;
    std::map<std::string, Import> m_imports;
    std::map<std::string, base::dbus::NameWatch> m_serviceWatches;
    std::string m_pendingKey;
    std::string m_pendingTitle;
    base::Timer m_pendingTimer;
    base::Timer m_updateTimer;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_APPLICATION_MENU_H_
