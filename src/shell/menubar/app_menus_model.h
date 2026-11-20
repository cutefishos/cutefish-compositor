// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_APP_MENUS_MODEL_H_
#define CUTEFISH_SHELL_MENUBAR_APP_MENUS_MODEL_H_

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

class AppMenusModel
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

    // `registrar` and `power` may be null.
    AppMenusModel(Shell *shell, AppMenuRegistrar *registrar, PowerActions *power);
    ~AppMenusModel();

    AppMenusModel(const AppMenusModel &) = delete;
    AppMenusModel &operator=(const AppMenusModel &) = delete;

    const std::vector<Row> &rows() const { return m_rows; }
    size_t count() const { return m_rows.size(); }

    bool hasMenu(size_t row) const;
    // Null for a plain action; made on first use and kept until the bar
    // switches.
    ui::Menu *menuAt(size_t row);
    std::optional<size_t> rowOf(const ui::Menu *menu) const;
    void trigger(size_t row);

    bool isDesktop() const { return m_desktop; }

    base::Signal<> rowsChanged;
    // Comes before rowsChanged.
    base::Signal<> switched;
    base::Signal<size_t> activationRequested;
    base::Signal<> menuClosed;

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
    std::optional<int32_t> successor(const std::vector<Row> &previous, int32_t id,
                                     const std::map<int32_t, std::unique_ptr<MenuPopup>> &carried) const;

    Shell *m_shell;
    PowerActions *m_power;
    std::unique_ptr<ApplicationMenu> m_applicationMenu;
    bool m_desktop = false;

    std::shared_ptr<compositor::MenuModel> m_desktopMainMenu;

    // Null for a bar with the name alone.
    compositor::MenuModel *m_shown = nullptr;
    std::vector<base::Subscription> m_shownSubscriptions;
    std::vector<Row> m_rows;
    base::Timer m_rowsTimer;

    std::unique_ptr<ui::Menu> m_userMenu;
    std::unique_ptr<ui::Menu> m_applicationRowMenu;
    // By entry id.
    std::map<int32_t, std::unique_ptr<MenuPopup>> m_popups;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_APP_MENUS_MODEL_H_
