// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/menubar/menu_bar_model.h"

#include <algorithm>

#include "base/i18n.h"
#include "compositor/menu_model.h"
#include "compositor/surface.h"
#include "shell/common/task_windows.h"
#include "shell/shell.h"
#include "shell/statusbar/application_menu.h"
#include "shell/statusbar/menu_popup.h"
#include "shell/statusbar/power_actions.h"
#include "ui/application.h"
#include "ui/menu.h"
#include "wm/window.h"
#include "wm/window_manager.h"

using cutefish::i18n::format;
using cutefish::i18n::tr;

namespace cutefish::shell {

MenuBarModel::MenuBarModel(Shell *shell, AppMenuRegistrar *registrar, PowerActions *power)
    : m_shell(shell)
    , m_power(power)
    , m_userMenu(std::make_unique<ui::Menu>())
    , m_applicationRowMenu(std::make_unique<ui::Menu>())
{
    m_applicationMenu = std::make_unique<ApplicationMenu>(shell->bus(), shell->taskWindows(), shell->tasks(),
                                                          registrar);
    buildUserMenu();
    // The application entry's menu names the application as it is when it
    // opens.
    m_applicationRowMenu->onAboutToShow = [this] { fillApplicationMenu(); };
    m_userMenu->onClosed = [this] { menuClosed(); };
    m_applicationRowMenu->onClosed = [this] { menuClosed(); };

    m_subscriptions.push_back(m_applicationMenu->switched.connect([this] { updateShown(); }));
    m_subscriptions.push_back(m_applicationMenu->titleChanged.connect([this] { updateRows(); }));
    m_subscriptions.push_back(m_applicationMenu->activationRequested.connect([this](int32_t id) {
        if (m_shown != m_applicationMenu->menu())
            return;
        for (size_t i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].kind != Kind::User && m_rows[i].id == id)
                activationRequested(i);
        }
    }));

    // ShellState's desktop: no application window has the keyboard and the
    // launcher does not cover the desktop.
    TaskWindows &windows = shell->taskWindows();
    m_subscriptions.push_back(windows.activeWindowChanged.connect([this](uint64_t) { updateDesktop(); }));
    m_subscriptions.push_back(windows.windowChanged.connect([this](uint64_t, uint32_t changes) {
        updateDesktop();
        if (changes & kApplicationMenuChange)
            updateDesktopMenu();
    }));
    m_subscriptions.push_back(windows.windowRemoved.connect([this](uint64_t) { updateDesktop(); }));
    if (wm::WindowManager *manager = shell->windowManager()) {
        m_subscriptions.push_back(manager->activeWindowChanged.connect([this](wm::Window *) { updateDesktop(); }));
        m_subscriptions.push_back(manager->windowAdded.connect([this](wm::Window *) {
            updateDesktop();
            updateDesktopMenu();
        }));
        m_subscriptions.push_back(manager->windowRemoved.connect([this](wm::Window *) {
            updateDesktop();
            updateDesktopMenu();
        }));
    }

    m_subscriptions.push_back(ui::Application::instance()->languageChanged.connect([this] { retranslate(); }));

    updateDesktop();
    updateDesktopMenu();
    updateShown();
}

MenuBarModel::~MenuBarModel()
{
    m_subscriptions.clear();
    m_shownSubscriptions.clear();
    m_popups.clear();
    m_userMenu->onClosed = nullptr;
    m_applicationRowMenu->onClosed = nullptr;
    m_userMenu->close();
    m_applicationRowMenu->close();
}

void MenuBarModel::buildUserMenu()
{
    ui::Menu &menu = *m_userMenu;
    Shell *shell = m_shell;
    menu.addItem(tr("About"), [shell] { shell->openSettings({"-m", "about"}); });
    menu.addItem(tr("Settings"), [shell] { shell->openSettings(); });
    menu.addSeparator();
    PowerActions *power = m_power;
    menu.addItem(tr("Suspend"), [power] {
        if (power)
            power->suspend();
    });
    menu.addItem(tr("Restart"), [power] {
        if (power)
            power->request(PowerActions::Reboot);
    });
    menu.addItem(tr("Shutdown"), [power] {
        if (power)
            power->request(PowerActions::Shutdown);
    });
    menu.addSeparator();
    menu.addItem(tr("Lock screen"), [power] {
        if (power)
            power->lockScreen();
    });
    menu.addItem(tr("Log out"), [power] {
        if (power)
            power->request(PowerActions::Logout);
    });
}

// The user's menu is made again, closed if it was open; the application
// entry's menu is made as it opens. The applications' own menus are theirs.
void MenuBarModel::retranslate()
{
    m_userMenu->close();
    m_userMenu->clear();
    buildUserMenu();
    m_applicationRowMenu->close();
    m_applicationMenu->retranslate();
}

void MenuBarModel::fillApplicationMenu()
{
    ui::Menu &menu = *m_applicationRowMenu;
    menu.clear();
    ApplicationMenu *application = m_applicationMenu.get();
    // Quit, Hide and Hide Others are the compositor's; on the desktop there
    // is nothing to quit or hide.
    const size_t hide = menu.addItem(format(tr("Hide {name}"), {{"name", application->title()}}),
                                     [application] { application->hideApplication(); });
    menu.addItem(tr("Hide Others"), [application] { application->hideOtherApplications(); });
    menu.addSeparator();
    const size_t quit = menu.addItem(tr("Quit"), [application] { application->quitApplication(); });
    menu.setItemEnabled(hide, !m_desktop);
    menu.setItemEnabled(quit, !m_desktop);
}

void MenuBarModel::updateDesktop()
{
    bool launcher = false;
    if (const wm::WindowManager *manager = m_shell->windowManager()) {
        for (const wm::Window *window : manager->windows()) {
            if (window->role() == wm::WindowRole::Launcher && window->isVisible())
                launcher = true;
        }
    }
    TaskWindows &windows = m_shell->taskWindows();
    const TaskWindow *active = windows.window(windows.activeWindow());
    const bool desktop = !launcher && !(active && active->isTaskListWindow());
    if (desktop == m_desktop)
        return;
    m_desktop = desktop;
    m_applicationMenu->setDesktop(desktop);
    // An application without menus leaves the application menu as it was
    // (none), so nothing else would take the desktop's menus down.
    updateShown();
}

void MenuBarModel::updateDesktopMenu()
{
    std::shared_ptr<compositor::MenuModel> mainMenu;
    if (const wm::WindowManager *manager = m_shell->windowManager()) {
        for (const wm::Window *window : manager->windows()) {
            compositor::Surface *surface = window->rootSurface();
            if (window->role() == wm::WindowRole::Desktop && surface && surface->mainMenu()) {
                mainMenu = surface->mainMenu();
                break;
            }
        }
    }
    if (mainMenu == m_desktopMainMenu)
        return;
    const bool wasShown = m_desktopMainMenu && m_shown == m_desktopMainMenu.get();
    std::shared_ptr<compositor::MenuModel> previous = std::exchange(m_desktopMainMenu, mainMenu);
    if (wasShown || m_desktop)
        updateShown();
}

void MenuBarModel::updateShown()
{
    compositor::MenuModel *shown = m_applicationMenu->menu();
    if (m_desktop)
        shown = m_desktopMainMenu.get();

    // ApplicationMenu says switched for any change of application, even to
    // the same menu; the open menu goes either way.
    // The previous menu's popups go after the view closed the open one, and
    // once the model is consistent: closing a menu reaches the window
    // manager, which may call back into here.
    std::map<int32_t, std::unique_ptr<MenuPopup>> previous;
    if (shown != m_shown) {
        m_shownSubscriptions.clear();
        m_shown = shown;
        watchShown();
        previous = std::move(m_popups);
        m_popups.clear();
    }
    switched();
    previous.clear();
    updateRows();
}

void MenuBarModel::watchShown()
{
    if (!m_shown)
        return;
    // The bar's entries are the root's children: a new layout of the root or
    // a change to one of them renames, adds or removes an entry.
    m_shownSubscriptions.push_back(m_shown->menuUpdated.connect([this](int32_t id) {
        if (id == 0)
            updateRows();
    }));
    m_shownSubscriptions.push_back(m_shown->itemChanged.connect([this](int32_t id) {
        const compositor::MenuItem *item = m_shown->item(id);
        if (item && item->parent == 0)
            updateRows();
    }));
}

void MenuBarModel::updateRows()
{
    std::vector<Row> rows;
    rows.push_back({Kind::User, {}, -1});
    const std::string &title = m_applicationMenu->title();
    if (!title.empty()) {
        rows.push_back({Kind::Application, title, -1});
        // Top-level menus the application hid have no entry; its own menu
        // opens from its name.
        if (m_shown) {
            for (int32_t child : m_shown->root().children) {
                const compositor::MenuItem *item = m_shown->item(child);
                if (!item || !item->visible)
                    continue;
                if (item->role == compositor::MenuItem::Role::Application && rows[1].id < 0)
                    rows[1].id = child;
                else
                    rows.push_back({Kind::Menu, compositor::plainMenuLabel(item->label), child});
            }
        }
    }
    if (rows == m_rows)
        return;
    m_rows = std::move(rows);

    // Menus of entries that are gone go with them.
    std::vector<std::unique_ptr<MenuPopup>> gone;
    for (auto it = m_popups.begin(); it != m_popups.end();) {
        const bool kept = std::any_of(m_rows.begin(), m_rows.end(),
                                      [&](const Row &row) { return row.kind != Kind::User && row.id == it->first; });
        if (kept) {
            ++it;
            continue;
        }
        gone.push_back(std::move(it->second));
        it = m_popups.erase(it);
    }
    rowsChanged();
    gone.clear();
}

ui::Menu *MenuBarModel::menuAt(size_t row)
{
    if (row >= m_rows.size())
        return nullptr;
    switch (m_rows[row].kind) {
    case Kind::User:
        return m_userMenu.get();
    case Kind::Application:
        if (m_rows[row].id < 0)
            return m_applicationRowMenu.get();
        break;
    case Kind::Menu:
        break;
    }
    const compositor::MenuItem *item = m_shown ? m_shown->item(m_rows[row].id) : nullptr;
    if (!item || !(item->hasSubmenu || !item->children.empty()))
        return nullptr;
    std::unique_ptr<MenuPopup> &popup = m_popups[item->id];
    if (!popup) {
        Shell *shell = m_shell;
        popup = std::make_unique<MenuPopup>(m_shown, item->id, [shell](const std::string &key, uint32_t modifiers) {
            return shell->shortcutText(key, modifiers, true);
        });
        popup->onClosed = [this] { menuClosed(); };
    }
    return &popup->menu();
}

void MenuBarModel::trigger(size_t row)
{
    if (row >= m_rows.size() || m_rows[row].kind != Kind::Menu || !m_shown)
        return;
    if (const compositor::MenuItem *item = m_shown->item(m_rows[row].id); item && item->enabled)
        m_shown->activate(item->id);
}

} // namespace cutefish::shell
