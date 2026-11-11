// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MENU_BAR_MODEL_H_
#define CUTEFISH_SHELL_MENUBAR_MENU_BAR_MODEL_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/signal.h"
#include "base/timer.h"

namespace cutefish::ui {
class Menu;
}

namespace cutefish::compositor {
class MenuModel;
}

namespace cutefish::shell {

class ApplicationMenu;
class AppMenuRegistrar;
class MenuPopup;
class PowerActions;
class Shell;

// The entries of the status bar's menu bar, left to right: the user's own
// menu, then the active application's name and its exported menus (on the
// backend's ApplicationMenu). The user row is
// always there; the application rows come and go with the focus.
//
// The desktop is not an application: while it has the focus the bar names
// "Desktop" and shows the desktop window's main menu.
class MenuBarModel
{
public:
    enum class Kind {
        User,
        Application,
        Menu,
    };

    struct Row {
        Kind kind = Kind::Menu;
        // With the exporter's "_" mnemonic markers removed.
        std::string title;
        // The dbusmenu entry of a Menu row, or of the application's own menu
        // on the Application row; -1 otherwise.
        int32_t id = -1;

        bool operator==(const Row &) const = default;
    };

    // `registrar` and `power` may be null (tests): no exported menus, and a
    // user menu whose power entries do nothing.
    MenuBarModel(Shell *shell, AppMenuRegistrar *registrar, PowerActions *power);
    ~MenuBarModel();

    MenuBarModel(const MenuBarModel &) = delete;
    MenuBarModel &operator=(const MenuBarModel &) = delete;

    const std::vector<Row> &rows() const { return m_rows; }
    size_t count() const { return m_rows.size(); }

    // Whether the entry at `row` opens a menu rather than being a plain
    // action.
    bool hasMenu(size_t row) const;
    // The menu of the entry at `row`, or null when the entry is a plain
    // action; made on first use and kept until the bar switches.
    ui::Menu *menuAt(size_t row);
    // The row whose menu `menu` is, if any.
    std::optional<size_t> rowOf(const ui::Menu *menu) const;
    // A plain action entry was clicked.
    void trigger(size_t row);

    // Whether the desktop has the focus (ShellState::isDesktop).
    bool isDesktop() const { return m_desktop; }

    // The rows changed: the bar switched, the name changed, or the shown
    // menu gained, lost or renamed an entry. Changes of the shown menu come
    // together on the next turn of the loop, and an open menu whose entry
    // was replaced by one of the same title stays open on the new one.
    base::Signal<> rowsChanged;
    // The bar now belongs to another application, or none: an open menu of
    // the previous one has to go. Comes before rowsChanged.
    base::Signal<> switched;
    // The application asks for the entry at `row` to be opened, which is how
    // a menu's shortcut (Alt and its mnemonic) arrives.
    base::Signal<size_t> activationRequested;
    // A menu menuAt() handed out closed: by a choice, Escape, a press
    // elsewhere, or close().
    base::Signal<> menuClosed;

    // The desktop window's main menu, while it has one; for tests.
    compositor::MenuModel *desktopMainMenu() const { return m_desktopMainMenu.get(); }

private:
    void buildUserMenu();
    void retranslate();
    void fillApplicationMenu();
    void updateDesktop();
    void updateDesktopMenu();
    void updateShown();
    void watchShown();
    void scheduleRows();
    void updateRows();
    // The id of the entry that took the place of `previous`'s entry `id`:
    // the same kind and title, a menu, and no popup yet.
    std::optional<int32_t> successor(const std::vector<Row> &previous, int32_t id,
                                     const std::map<int32_t, std::unique_ptr<MenuPopup>> &carried) const;

    Shell *m_shell;
    PowerActions *m_power;
    std::unique_ptr<ApplicationMenu> m_applicationMenu;
    bool m_desktop = false;

    std::shared_ptr<compositor::MenuModel> m_desktopMainMenu;

    // The menu the application rows come from: the application's or the
    // desktop's; null for a bar with the name alone.
    compositor::MenuModel *m_shown = nullptr;
    std::vector<base::Subscription> m_shownSubscriptions;
    std::vector<Row> m_rows;
    base::Timer m_rowsTimer;

    std::unique_ptr<ui::Menu> m_userMenu;
    std::unique_ptr<ui::Menu> m_applicationRowMenu;
    // The open-able menus of the shown menu's entries, by entry id.
    std::map<int32_t, std::unique_ptr<MenuPopup>> m_popups;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MENU_BAR_MODEL_H_
