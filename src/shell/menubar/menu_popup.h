// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MENU_POPUP_H_
#define CUTEFISH_SHELL_MENUBAR_MENU_POPUP_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/signal.h"

namespace cutefish::compositor {
class MenuModel;
}

namespace cutefish::ui {
class Menu;
}

namespace cutefish::shell {

class MenuPopup
{
public:
    // Empty keeps the toolkit's.
    using ShortcutText = std::function<std::string(const std::string &key, uint32_t modifiers)>;

    MenuPopup(compositor::MenuModel *menu, int32_t id, ShortcutText shortcutText = {});
    ~MenuPopup();

    MenuPopup(const MenuPopup &) = delete;
    MenuPopup &operator=(const MenuPopup &) = delete;

    ui::Menu &menu() { return *m_menu; }

    void detach();
    void setPrepared();
    void rebind(int32_t id);

    std::function<void()> onClosed;

private:
    struct State;

    static void fill(const std::shared_ptr<State> &state, ui::Menu &menu, int32_t id);
    static void wire(const std::shared_ptr<State> &state, ui::Menu &menu, int32_t id, bool root);
    static void repoint(const std::shared_ptr<State> &state, const std::vector<std::pair<int32_t, int32_t>> &moves);
    static void refill(const std::shared_ptr<State> &state, int32_t id);
    static void update(const std::shared_ptr<State> &state, int32_t id);

    int32_t m_id;
    std::shared_ptr<State> m_state;
    std::unique_ptr<ui::Menu> m_menu;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MENU_POPUP_H_
