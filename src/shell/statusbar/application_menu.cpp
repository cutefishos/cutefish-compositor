// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/statusbar/application_menu.h"

#include <set>

#include "base/i18n.h"
#include "compositor/menu_model.h"
#include "shell/common/task_manager.h"
#include "shell/common/task_windows.h"
#include "shell/statusbar/app_menu_registrar.h"
#include "shell/statusbar/dbus_menu.h"

using cutefish::i18n::tr;

namespace cutefish::shell {

ApplicationMenu::ApplicationMenu(base::dbus::Connection *bus, TaskWindows &windows, TaskManager &tasks,
                                 AppMenuRegistrar *registrar)
    : m_bus(bus)
    , m_windows(windows)
    , m_tasks(tasks)
    , m_registrar(registrar)
{
    m_pendingTimer.setSingleShot(true);
    m_pendingTimer.onTimeout = [this] {
        if (!m_pendingKey.empty())
            commit(m_pendingTitle, nullptr);
    };
    m_updateTimer.setSingleShot(true);
    m_updateTimer.onTimeout = [this] {
        update();
        pruneImports();
    };

    m_subscriptions.push_back(m_windows.activeWindowChanged.connect([this](uint64_t) { scheduleUpdate(); }));
    m_subscriptions.push_back(m_windows.windowChanged.connect([this](uint64_t id, uint32_t changes) {
        constexpr uint32_t relevant =
                kTitleChange | kAppIdChange | kPidChange | kApplicationMenuChange | kSkipTaskbarChange;
        if (id == m_windows.activeWindow() && (changes & relevant))
            scheduleUpdate();
        if (changes & (kApplicationMenuChange | kSkipTaskbarChange))
            prefetch(id);
    }));
    m_subscriptions.push_back(m_windows.windowAdded.connect([this](uint64_t id) { prefetch(id); }));
    m_subscriptions.push_back(m_windows.windowRemoved.connect([this](uint64_t) { scheduleUpdate(); }));
    m_subscriptions.push_back(m_tasks.windowEntryChanged.connect([this](uint64_t id) {
        if (id == m_windows.activeWindow())
            scheduleUpdate();
    }));
    if (m_registrar) {
        m_subscriptions.push_back(m_registrar->windowRegistered.connect(
                [this](uint32_t, const std::string &service, const std::string &path) {
                    importFor(service, path);
                    scheduleUpdate();
                }));
        m_subscriptions.push_back(m_registrar->windowUnregistered.connect([this](uint32_t) { scheduleUpdate(); }));
    }

    for (uint64_t id : m_windows.windows())
        prefetch(id);
    update();
}

ApplicationMenu::~ApplicationMenu() = default;

std::string ApplicationMenu::importKey(const std::string &service, const std::string &path)
{
    return service + '\n' + path;
}

void ApplicationMenu::setDesktop(bool desktop)
{
    if (m_desktop == desktop)
        return;
    m_desktop = desktop;
    scheduleUpdate();
}

void ApplicationMenu::retranslate()
{
    if (m_desktop)
        scheduleUpdate();
}

void ApplicationMenu::menuAddressOf(uint64_t window, std::string *service, std::string *path) const
{
    const TaskWindow *client = m_windows.window(window);
    if (!client)
        return;
    *service = client->menuService;
    *path = client->menuPath;
    // Not every toolkit says which menu a window has; the registrar knows
    // the process that exported one.
    if ((service->empty() || path->empty()) && client->pid > 0 && m_registrar) {
        *service = m_registrar->serviceForPid(uint32_t(client->pid));
        *path = m_registrar->pathForPid(uint32_t(client->pid));
    }
}

ApplicationMenu::Import *ApplicationMenu::importFor(const std::string &service, const std::string &path)
{
    if (service.empty() || path.empty() || !m_bus)
        return nullptr;

    const std::string key = importKey(service, path);
    if (const auto it = m_imports.find(key); it != m_imports.end())
        return &it->second;

    Import &import = m_imports[key];
    import.menu = std::make_unique<DBusMenu>(m_bus, service, path);
    DBusMenu *menu = import.menu.get();
    import.subscriptions.push_back(menu->menuUpdated.connect([this, key](int32_t id) { onImported(key, id); }));
    import.subscriptions.push_back(menu->activationRequested.connect([this, menu](int32_t id) {
        const compositor::MenuItem *item = menu->item(id);
        if (menu == m_menu && item && item->parent == 0)
            activationRequested(id);
    }));

    if (!m_serviceWatches.count(service)) {
        m_serviceWatches.emplace(service, m_bus->watchName(service, [this, service](const std::string &owner) {
            if (owner.empty())
                dropService(service);
        }));
    }
    return &import;
}

void ApplicationMenu::prefetch(uint64_t window)
{
    const TaskWindow *client = m_windows.window(window);
    if (client && client->isTaskListWindow())
        importFor(client->menuService, client->menuPath);
}

void ApplicationMenu::onImported(const std::string &key, int32_t id)
{
    // Submenus report too; only the root is the bar.
    const auto it = m_imports.find(key);
    if (it == m_imports.end() || id != 0)
        return;
    Import &import = it->second;
    import.ready = true;

    // The first level of submenus is what the bar pops up: have the ones not
    // fetched yet ready, without an "opened" that no "closed" would follow.
    // One fetched before waits for its next show: its application may answer
    // being asked by laying the bar out again.
    for (int32_t child : import.menu->root().children) {
        const compositor::MenuItem *item = import.menu->item(child);
        if (item && item->hasSubmenu && !item->childrenLoaded && item->children.empty())
            import.menu->prepare(child);
    }

    if (key == m_pendingKey)
        commitPending();
}

void ApplicationMenu::dropService(const std::string &service)
{
    bool shownGone = false;
    const std::string prefix = service + '\n';
    // Kept until switched() is out: the menus open on them close there.
    std::vector<Import> gone;
    for (auto it = m_imports.begin(); it != m_imports.end();) {
        if (!it->first.starts_with(prefix)) {
            ++it;
            continue;
        }
        if (it->second.menu.get() == m_menu) {
            shownGone = true;
            m_menu = nullptr;
        }
        if (it->first == m_pendingKey)
            m_pendingKey.clear();
        it->second.subscriptions.clear();
        gone.push_back(std::move(it->second));
        it = m_imports.erase(it);
    }

    // The exporter went away with its application, whose windows are on
    // their way out too; the next focus change picks the new bar.
    if (shownGone)
        switched();
    gone.clear();

    // Last: the watch is what called.
    m_serviceWatches.erase(service);
}

void ApplicationMenu::scheduleUpdate()
{
    if (!m_updateTimer.isActive())
        m_updateTimer.start(std::chrono::nanoseconds(0));
}

// An import whose windows are all gone is dropped: the service watch only
// covers the whole exporting process leaving, and a long-lived application
// exports one path per window it ever opened.
void ApplicationMenu::pruneImports()
{
    std::set<std::string> used;
    for (uint64_t window : m_windows.windows()) {
        std::string service;
        std::string path;
        menuAddressOf(window, &service, &path);
        if (!service.empty() && !path.empty())
            used.insert(importKey(service, path));
    }
    for (auto it = m_imports.begin(); it != m_imports.end();) {
        if (used.count(it->first) || it->second.menu.get() == m_menu || it->first == m_pendingKey) {
            ++it;
            continue;
        }
        it = m_imports.erase(it);
    }
}

void ApplicationMenu::update()
{
    // Whatever was waited for is superseded unless it is chosen again below.
    const std::string previousPending = std::exchange(m_pendingKey, std::string());
    m_pendingTimer.stop();

    const std::string title = applicationTitle();

    // The desktop's menus are not behind D-Bus.
    if (m_desktop) {
        commit(title, nullptr);
        return;
    }
    if (title.empty()) {
        commit({}, nullptr);
        return;
    }

    if (const TaskWindow *client = m_windows.window(m_windows.activeWindow()); client && client->mainMenu) {
        commit(title, client->mainMenu.get(), client->mainMenu);
        return;
    }

    std::string service;
    std::string path;
    menuAddressOf(m_windows.activeWindow(), &service, &path);
    Import *import = importFor(service, path);
    if (!import) {
        commit(title, nullptr);
        return;
    }
    if (import->ready) {
        commit(title, import->menu.get());
        return;
    }

    // Keep the previous bar rather than show this name over its menus. A
    // wait that already ran out shows the name alone until the menu lands.
    const std::string key = importKey(service, path);
    m_pendingKey = key;
    m_pendingTitle = title;
    if (key != previousPending)
        m_pendingTimer.start(kPendingMenuTimeout);
    else if (m_title != title)
        commit(title, nullptr);
}

void ApplicationMenu::commitPending()
{
    const auto it = m_imports.find(m_pendingKey);
    m_pendingKey.clear();
    m_pendingTimer.stop();
    if (it != m_imports.end())
        commit(m_pendingTitle, it->second.menu.get());
}

void ApplicationMenu::commit(const std::string &title, compositor::MenuModel *menu,
                             std::shared_ptr<compositor::MenuModel> held)
{
    const bool shown = !title.empty();
    if (shown == m_shown && menu == m_menu) {
        if (title != m_title) {
            m_title = title;
            titleChanged();
        }
        return;
    }
    m_title = title;
    m_menu = menu;
    m_shown = shown;
    // Released after the switch: the menus open on the previous one close
    // there.
    std::shared_ptr<compositor::MenuModel> previous = std::exchange(m_heldMenu, std::move(held));
    switched();
}

std::string ApplicationMenu::applicationTitle() const
{
    if (m_desktop)
        return tr("Desktop");
    const uint64_t window = m_windows.activeWindow();
    const TaskWindow *client = m_windows.window(window);
    if (!client || !client->isTaskListWindow())
        return {};
    if (const DesktopEntryPtr entry = m_tasks.entryForWindow(window))
        return entry->name;
    // The application's own menu carries its name.
    if (client->mainMenu) {
        for (int32_t id : client->mainMenu->root().children) {
            const compositor::MenuItem *item = client->mainMenu->item(id);
            if (item && item->role == compositor::MenuItem::Role::Application && !item->label.empty())
                return compositor::plainMenuLabel(item->label);
        }
    }
    return client->title;
}

void ApplicationMenu::quitApplication()
{
    const uint64_t window = m_windows.activeWindow();
    const TaskWindow *client = m_windows.window(window);
    if (client && client->isTaskListWindow())
        m_windows.quitApplication(window);
}

void ApplicationMenu::hideApplication()
{
    const uint64_t window = m_windows.activeWindow();
    const TaskWindow *client = m_windows.window(window);
    if (!m_desktop && client && client->isTaskListWindow())
        m_windows.hideApplication(window);
}

void ApplicationMenu::hideOtherApplications()
{
    const uint64_t window = m_windows.activeWindow();
    const TaskWindow *client = m_windows.window(window);
    // On the desktop there is no active application: every window goes.
    const bool keepActive = !m_desktop && client && client->isTaskListWindow();
    m_windows.hideOtherApplications(keepActive ? window : 0);
}

} // namespace cutefish::shell
