// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_STATUSBAR_MENU_POPUP_H_
#define CUTEFISH_SHELL_STATUSBAR_MENU_POPUP_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"

namespace cutefish::compositor {
class MenuModel;
}

namespace cutefish::ui {
class Menu;
}

namespace cutefish::shell {

// One menu of an application's menu model shown as a toolkit menu. Rows are
// built from the model whenever the menu or one of its submenus opens, and
// again while it is open as the application changes its entries; the
// application hears that the menu is about to show, opened and closed, and a
// chosen row is activated. Hidden entries have no row, and separators only
// stand between two entries.
class MenuPopup
{
public:
    // The text for a key equivalent; empty keeps the toolkit's.
    using ShortcutText = std::function<std::string(const std::string &key, uint32_t modifiers)>;

    MenuPopup(compositor::MenuModel *menu, int32_t id, ShortcutText shortcutText = {});
    ~MenuPopup();

    MenuPopup(const MenuPopup &) = delete;
    MenuPopup &operator=(const MenuPopup &) = delete;

    ui::Menu &menu() { return *m_menu; }

    // The model is gone: nothing reaches it from here on.
    void detach();
    // The opener already prepared the menu and waited for its entries: its
    // next opening only says it opened.
    void setPrepared();

    // The menu closed, by a choice, Escape or a press elsewhere.
    std::function<void()> onClosed;

private:
    struct State;

    static void fill(const std::shared_ptr<State> &state, ui::Menu &menu, int32_t id);
    static void wire(const std::shared_ptr<State> &state, ui::Menu &menu, int32_t id);
    // The open menu showing `id`, if any, brought up to date.
    static void update(const std::shared_ptr<State> &state, int32_t id);

    int32_t m_id;
    std::shared_ptr<State> m_state;
    std::unique_ptr<ui::Menu> m_menu;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_STATUSBAR_MENU_POPUP_H_
