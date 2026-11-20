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

class AppMenusView final : public MenuBarAppMenus
{
public:
    static constexpr double kPopupMargin = 6;

    AppMenusView(Shell *shell, MenuBarAppMenusHost host);
    ~AppMenusView() override;

    void closeMenu() override { close(); }
    void barChanged() override;

    void open(int index, bool keyboard = false);
    void close();
    void step(int step);

    // The entry whose menu is open, or -1.
    int currentIndex() const { return m_currentIndex; }
    ui::Menu *openMenu() const { return m_openMenu; }
    AppMenusModel &model() { return *m_model; }
    BarItem *entry(size_t index) const;
    // Empty shows the account's colour.
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
    bool m_switching = false;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_APP_MENUS_VIEW_H_
