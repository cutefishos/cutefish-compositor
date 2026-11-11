// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/menubar/menu_bar_model.h"

#include <algorithm>
#include <chrono>

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
    m_rowsTimer.setSingleShot(true);
    m_rowsTimer.onTimeout = [this] { updateRows(); };

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
    // a change to one of them renames, adds or removes an entry. An
    // application may rebuild its bar one entry at a time.
    m_shownSubscriptions.push_back(m_shown->menuUpdated.connect([this](int32_t id) {
        if (id == 0)
            scheduleRows();
    }));
    m_shownSubscriptions.push_back(m_shown->itemChanged.connect([this](int32_t id) {
        const compositor::MenuItem *item = m_shown->item(id);
        if (item && item->parent == 0)
            scheduleRows();
    }));
}

void MenuBarModel::scheduleRows()
{
    if (!m_rowsTimer.isActive())
        m_rowsTimer.start(std::chrono::nanoseconds(0));
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
    const std::vector<Row> previous = std::exchange(m_rows, std::move(rows));

    // Menus of entries that are gone go with them, except an open one whose
    // entry came back under another id: it shows that one from now on.
    std::vector<std::unique_ptr<MenuPopup>> gone;
    std::map<int32_t, std::unique_ptr<MenuPopup>> carried;
    for (auto it = m_popups.begin(); it != m_popups.end();) {
        const bool kept = std::any_of(m_rows.begin(), m_rows.end(),
                                      [&](const Row &row) { return row.kind != Kind::User && row.id == it->first; });
        if (kept) {
            ++it;
            continue;
        }
        const std::optional<int32_t> id =
                it->second->menu().isOpen() ? successor(previous, it->first, carried) : std::nullopt;
        if (id) {
            it->second->rebind(*id);
            carried[*id] = std::move(it->second);
        } else {
            gone.push_back(std::move(it->second));
        }
        it = m_popups.erase(it);
    }
    m_popups.merge(carried);
    rowsChanged();
    gone.clear();
}

std::optional<int32_t> MenuBarModel::successor(const std::vector<Row> &previous, int32_t id,
                                               const std::map<int32_t, std::unique_ptr<MenuPopup>> &carried) const
{
    const auto old = std::find_if(previous.begin(), previous.end(),
                                  [&](const Row &row) { return row.kind != Kind::User && row.id == id; });
    if (old == previous.end())
        return std::nullopt;
    const auto fits = [&](size_t index) {
        const Row &row = m_rows[index];
        return row.kind == old->kind && row.title == old->title && row.id >= 0 && !m_popups.count(row.id)
                && !carried.count(row.id) && hasMenu(index);
    };
    const size_t index = size_t(old - previous.begin());
    if (index < m_rows.size() && fits(index))
        return m_rows[index].id;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (fits(i))
            return m_rows[i].id;
    }
    return std::nullopt;
}

bool MenuBarModel::hasMenu(size_t row) const
{
    if (row >= m_rows.size())
        return false;
    if (m_rows[row].kind == Kind::User || (m_rows[row].kind == Kind::Application && m_rows[row].id < 0))
        return true;
    const compositor::MenuItem *item = m_shown ? m_shown->item(m_rows[row].id) : nullptr;
    return item && (item->hasSubmenu || !item->children.empty());
}

ui::Menu *MenuBarModel::menuAt(size_t row)
{
    if (!hasMenu(row))
        return nullptr;
    if (m_rows[row].kind == Kind::User)
        return m_userMenu.get();
    if (m_rows[row].kind == Kind::Application && m_rows[row].id < 0)
        return m_applicationRowMenu.get();
    const int32_t id = m_rows[row].id;
    std::unique_ptr<MenuPopup> &popup = m_popups[id];
    if (!popup) {
        Shell *shell = m_shell;
        popup = std::make_unique<MenuPopup>(m_shown, id, [shell](const std::string &key, uint32_t modifiers) {
            return shell->shortcutText(key, modifiers, true);
        });
        popup->onClosed = [this] { menuClosed(); };
    }
    return &popup->menu();
}

std::optional<size_t> MenuBarModel::rowOf(const ui::Menu *menu) const
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows[i];
        if (row.kind == Kind::User) {
            if (menu == m_userMenu.get())
                return i;
        } else if (row.kind == Kind::Application && row.id < 0) {
            if (menu == m_applicationRowMenu.get())
                return i;
        } else if (const auto it = m_popups.find(row.id); it != m_popups.end() && &it->second->menu() == menu) {
            return i;
        }
    }
    return std::nullopt;
}

void MenuBarModel::trigger(size_t row)
{
    if (row >= m_rows.size() || m_rows[row].kind != Kind::Menu || !m_shown)
        return;
    if (const compositor::MenuItem *item = m_shown->item(m_rows[row].id); item && item->enabled)
        m_shown->activate(item->id);
}

} // namespace cutefish::shell
