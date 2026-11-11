// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/statusbar/menu_popup.h"

#include <map>

#include "compositor/menu_model.h"
#include "ui/event.h"
#include "ui/icon.h"
#include "ui/menu.h"

namespace cutefish::shell {

namespace {

struct KeyEquivalent {
    std::string key;
    uint32_t modifiers = 0;
};

ui::MenuItemState stateOf(const compositor::MenuItem &item)
{
    if (item.toggle == compositor::MenuItem::Toggle::None || item.toggleState == 0)
        return ui::MenuItemState::Off;
    return item.toggleState == 1 ? ui::MenuItemState::On : ui::MenuItemState::Mixed;
}

// The first of the entry's key combinations.
KeyEquivalent keyEquivalentOf(const compositor::MenuItem &item)
{
    KeyEquivalent result;
    if (item.shortcut.empty() || item.shortcut.front().empty())
        return result;
    const std::vector<std::string> &keys = item.shortcut.front();
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (keys[i] == "Control")
            result.modifiers |= ui::ControlModifier;
        else if (keys[i] == "Alt")
            result.modifiers |= ui::AltModifier;
        else if (keys[i] == "Shift")
            result.modifiers |= ui::ShiftModifier;
        else if (keys[i] == "Super")
            result.modifiers |= ui::SuperModifier;
    }
    result.key = keys.back();
    return result;
}

} // namespace

// What the menus' closures share with the popup: the model is null once it
// or the popup is gone, so a menu still on its way out reaches nothing.
struct MenuPopup::State {
    // The entry a toolkit menu shows, read by its closures as they run, so
    // that the menu can move to an entry that took its place.
    struct Binding {
        int32_t id = 0;
        bool root = false;
    };

    // A toolkit menu showing one entry's submenu, and the entries its rows
    // were made from (-1 for a separator). `alive` expires with the menu.
    struct Shown {
        ui::Menu *menu = nullptr;
        std::weak_ptr<void> alive;
        std::shared_ptr<Binding> binding;
        std::vector<int32_t> rows;
        std::vector<bool> submenus;
    };

    compositor::MenuModel *model = nullptr;
    MenuPopup *owner = nullptr;
    MenuPopup::ShortcutText shortcutText;
    bool prepared = false;
    std::map<int32_t, Shown> shown;
};

MenuPopup::MenuPopup(compositor::MenuModel *menu, int32_t id, ShortcutText shortcutText)
    : m_id(id)
    , m_state(std::make_shared<State>())
    , m_menu(std::make_unique<ui::Menu>())
{
    m_state->model = menu;
    m_state->owner = this;
    m_state->shortcutText = std::move(shortcutText);
    wire(m_state, *m_menu, m_id, true);
    std::weak_ptr<State> weak = m_state;
    m_subscriptions.push_back(menu->menuUpdated.connect([weak](int32_t updated) {
        if (const std::shared_ptr<State> state = weak.lock())
            update(state, updated);
    }));
    m_subscriptions.push_back(menu->itemChanged.connect([weak](int32_t changed) {
        const std::shared_ptr<State> state = weak.lock();
        if (!state || !state->model)
            return;
        if (const compositor::MenuItem *item = state->model->item(changed))
            update(state, item->parent);
    }));
}

MenuPopup::~MenuPopup()
{
    m_subscriptions.clear();
    m_state->model = nullptr;
    m_state->owner = nullptr;
    m_menu->onClosed = nullptr;
    m_menu->close();
}

void MenuPopup::detach()
{
    m_subscriptions.clear();
    m_state->model = nullptr;
}

void MenuPopup::setPrepared()
{
    m_state->prepared = true;
}

void MenuPopup::rebind(int32_t id)
{
    if (id == m_id)
        return;
    repoint(m_state, {{m_id, id}});
    m_id = id;
    refill(m_state, m_id);
}

void MenuPopup::wire(const std::shared_ptr<State> &state, ui::Menu &menu, int32_t id, bool root)
{
    ui::Menu *target = &menu;
    // The token lives in the menu's own closure, so it expires with the menu.
    auto alive = std::make_shared<char>(0);
    auto binding = std::make_shared<State::Binding>(State::Binding {id, root});
    state->shown[id] = {target, alive, binding, {}, {}};
    // The application fills the menu as it is about to show, and the rows
    // show what has arrived so far.
    menu.onAboutToShow = [state, target, binding] {
        if (!state->model)
            return;
        if (binding->root && state->prepared) {
            state->prepared = false;
            state->model->opened(binding->id);
        } else {
            state->model->aboutToShow(binding->id);
        }
        fill(state, *target, binding->id);
    };
    menu.onClosed = [state, binding, alive] {
        if (!state->model)
            return;
        state->model->closed(binding->id);
        if (binding->root && state->owner && state->owner->onClosed)
            state->owner->onClosed();
    };
}

void MenuPopup::repoint(const std::shared_ptr<State> &state, const std::vector<std::pair<int32_t, int32_t>> &moves)
{
    // All taken out before any goes back, as two entries may swap ids.
    std::vector<std::pair<int32_t, State::Shown>> moved;
    for (const auto &[from, to] : moves) {
        const auto it = state->shown.find(from);
        if (it == state->shown.end())
            continue;
        moved.emplace_back(to, std::move(it->second));
        state->shown.erase(it);
    }
    for (auto &[to, shown] : moved) {
        shown.binding->id = to;
        state->shown[to] = std::move(shown);
    }
    std::erase_if(state->shown, [](const auto &entry) { return entry.second.alive.expired(); });
}

void MenuPopup::refill(const std::shared_ptr<State> &state, int32_t id)
{
    const auto it = state->shown.find(id);
    const compositor::MenuItem *item = state->model ? state->model->item(id) : nullptr;
    if (it == state->shown.end() || !item)
        return;
    if (!item->childrenLoaded) {
        state->model->refresh(id);
        return;
    }
    ui::Menu *menu = it->second.menu;
    fill(state, *menu, id);
    if (menu->isOpen())
        menu->itemsChanged();
}

void MenuPopup::update(const std::shared_ptr<State> &state, int32_t id)
{
    const auto it = state->shown.find(id);
    if (it == state->shown.end())
        return;
    if (it->second.alive.expired()) {
        state->shown.erase(it);
        return;
    }
    ui::Menu *menu = it->second.menu;
    if (!menu->isOpen())
        return;
    fill(state, *menu, id);
    menu->itemsChanged();
}

void MenuPopup::fill(const std::shared_ptr<State> &state, ui::Menu &menu, int32_t id)
{
    compositor::MenuModel *model = state->model;
    const compositor::MenuItem *parent = model ? model->item(id) : nullptr;
    if (!parent) {
        menu.clear();
        return;
    }
    // Until its entries are in, the menu keeps the rows it has.
    if (!parent->childrenLoaded)
        return;

    std::vector<const compositor::MenuItem *> rows;
    for (int32_t childId : parent->children) {
        const compositor::MenuItem *child = model->item(childId);
        if (!child || !child->visible)
            continue;
        // A separator is only worth a line between two entries.
        if (child->separator && (rows.empty() || rows.back()->separator))
            continue;
        rows.push_back(child);
    }
    while (!rows.empty() && rows.back()->separator)
        rows.pop_back();

    std::vector<int32_t> ids;
    std::vector<bool> submenus;
    for (const compositor::MenuItem *row : rows) {
        ids.push_back(row->separator ? -1 : row->id);
        submenus.push_back(!row->separator && (row->hasSubmenu || !row->children.empty()));
    }

    // Rows of the same kinds as before change in place, so an open submenu
    // stays open; one whose entry has a new id follows it.
    State::Shown &shown = state->shown[id];
    bool rebuild = ids.size() != shown.rows.size() || submenus != shown.submenus;
    for (size_t index = 0; !rebuild && index < ids.size(); ++index)
        rebuild = (ids[index] < 0) != (shown.rows[index] < 0);
    std::vector<std::pair<int32_t, int32_t>> moves;
    for (size_t index = 0; !rebuild && index < ids.size(); ++index) {
        if (submenus[index] && ids[index] != shown.rows[index])
            moves.emplace_back(shown.rows[index], ids[index]);
    }
    const std::shared_ptr<State::Binding> binding = shown.binding;
    shown.rows = ids;
    shown.submenus = submenus;
    if (rebuild) {
        menu.clear();
        std::erase_if(state->shown, [](const auto &entry) { return entry.second.alive.expired(); });
    }

    // Rows are counted as the menu counts them, separators included.
    for (size_t index = 0; index < rows.size(); ++index) {
        const compositor::MenuItem *row = rows[index];
        if (row->separator) {
            if (rebuild)
                menu.addSeparator();
            continue;
        }
        const std::string text = compositor::plainMenuLabel(row->label);
        if (rebuild) {
            if (submenus[index]) {
                wire(state, *menu.addSubmenu(text), row->id, false);
                // Its rows ready for when it opens.
                if (!row->childrenLoaded)
                    model->refresh(row->id);
            } else {
                menu.addItem(text, [state, binding, index] {
                    if (!state->model)
                        return;
                    const auto it = state->shown.find(binding->id);
                    if (it != state->shown.end() && index < it->second.rows.size())
                        state->model->activate(it->second.rows[index]);
                });
            }
        } else {
            menu.setItemText(index, text);
        }
        menu.setItemEnabled(index, row->enabled);
        menu.setItemState(index, stateOf(*row));
        const KeyEquivalent key = keyEquivalentOf(*row);
        menu.setItemKeyEquivalent(index, key.key, key.modifiers);
        if (state->shortcutText && !key.key.empty()) {
            const std::string text = state->shortcutText(key.key, key.modifiers);
            if (!text.empty())
                menu.setItemShortcut(index, text);
        }
        // A name the theme has; a picture of the application's own is not
        // shown.
        const bool themed = !row->iconName.empty() && !ui::themeIconPath(row->iconName, 22).empty();
        if (themed || !rebuild)
            menu.setItemIcon(index, themed ? row->iconName : std::string());
    }

    repoint(state, moves);
    for (const auto &move : moves)
        refill(state, move.second);
}

} // namespace cutefish::shell
