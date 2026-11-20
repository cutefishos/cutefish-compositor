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
#include "compositor/menu_model.h"

namespace cutefish::shell {

class AppMenuRegistrar;
class TaskManager;
class TaskWindows;

// Every input is handled on the next turn of the loop, so that a burst becomes
// one switch.
class ApplicationMenu
{
public:
    static constexpr std::chrono::milliseconds kPendingMenuTimeout{250};

    ApplicationMenu(base::dbus::Connection *bus, TaskWindows &windows, TaskManager &tasks,
                    AppMenuRegistrar *registrar);
    ~ApplicationMenu();

    ApplicationMenu(const ApplicationMenu &) = delete;
    ApplicationMenu &operator=(const ApplicationMenu &) = delete;

    const std::string &title() const { return m_title; }
    // Null for none.
    compositor::MenuModel *menu() const { return m_menu; }

    void setDesktop(bool desktop);
    void retranslate();

    void quitApplication();
    void hideApplication();
    void hideOtherApplications();

    base::Signal<> switched;
    base::Signal<> titleChanged;
    base::Signal<int32_t> activationRequested;

private:
    struct Import {
        std::unique_ptr<compositor::MenuModel> menu;
        bool ready = false;
        std::vector<base::Subscription> subscriptions;
    };

    struct Address {
        compositor::GtkMenuAddress gtk;
        std::string service;
        std::string path;
    };

    // Empty for no menu.
    static std::string importKey(const Address &address);
    Import *importFor(const Address &address);
    Address addressOf(uint64_t window) const;
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
