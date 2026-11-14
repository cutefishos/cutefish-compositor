// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_APP_MENUS_VIEW_H_
#define CUTEFISH_SHELL_MENUBAR_APP_MENUS_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"
#include "shell/menubar/app_menus_model.h"
#include "shell/menubar/menu_bar_app_menus.h"
#include "ui/geometry.h"

namespace cutefish::accounts {
class UserAccount;
}

namespace cutefish::ui {
class Menu;
}

namespace cutefish::shell {

class BarItem;
class Shell;

// The menu bar's app menus: the user's menu, then the
// active application's name and its menus, entries abutting each other from
// the bar's left edge. A click opens an entry's menu under its plate; while
// one is open, moving onto another entry or Left and Right switch to it.
//
// Goes into the bar's first slot: `bar->setAppMenus(std::make_unique<
// AppMenusView>(shell, bar->appMenusHost()))`. It owns the bar's model and
// with it the application menu backend.
class AppMenusView final : public MenuBarAppMenus
{
public:
    // The popup's distance below the bar (PanelStyle.popupMargin).
    static constexpr double kPopupMargin = 6;

    AppMenusView(Shell *shell, MenuBarAppMenusHost host);
    ~AppMenusView() override;

    // MenuBarAppMenus
    void closeMenu() override { close(); }
    void barChanged() override;

    // Opens the menu of the entry at `index` (nothing for a plain action
    // entry, which only a click chooses), closing the bar's panels and the
    // tray's menu.
    void open(int index, bool keyboard = false);
    void close();
    // Left and Right: the neighbouring entry with a menu, wrapping around.
    void step(int step);

    // The entry whose menu is open, or -1.
    int currentIndex() const { return m_currentIndex; }
    ui::Menu *openMenu() const { return m_openMenu; }
    AppMenusModel &model() { return *m_model; }
    BarItem *entry(size_t index) const;
    // The avatar's file; empty shows the account's colour.
    void setUserIcon(const std::string &file);

protected:
    void layout() override;

private:
    class Entry;

    void rebuildEntries();
    void onMenuClosed();

    MenuBarAppMenusHost m_host;
    std::unique_ptr<AppMenusModel> m_model;
    std::unique_ptr<accounts::UserAccount> m_account;
    std::vector<Entry *> m_entries;
    std::string m_userIcon;
    ui::Color m_userColor;
    int m_currentIndex = -1;
    ui::Menu *m_openMenu = nullptr;
    // A switch closes one menu to open the next: that close is not the
    // user's.
    bool m_switching = false;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_APP_MENUS_VIEW_H_
