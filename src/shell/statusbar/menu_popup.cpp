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
    // A toolkit menu showing one entry's submenu, and the entries its rows
    // were made from (-1 for a separator). `alive` expires with the menu.
    struct Shown {
        ui::Menu *menu = nullptr;
        std::weak_ptr<void> alive;
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
    wire(m_state, *m_menu, m_id);
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

void MenuPopup::wire(const std::shared_ptr<State> &state, ui::Menu &menu, int32_t id)
{
    ui::Menu *target = &menu;
    // The token lives in the menu's own closure, so it expires with the menu.
    auto alive = std::make_shared<char>(0);
    state->shown[id] = {target, alive, {}, {}};
    // The application fills the menu as it is about to show, and the rows
    // show what has arrived so far.
    const bool root = id == (state->owner ? state->owner->m_id : -1);
    menu.onAboutToShow = [state, target, id, root] {
        if (!state->model)
            return;
        if (root && state->prepared) {
            state->prepared = false;
            state->model->opened(id);
        } else {
            state->model->aboutToShow(id);
        }
        fill(state, *target, id);
    };
    menu.onClosed = [state, id, root, alive] {
        if (!state->model)
            return;
        state->model->closed(id);
        if (root && state->owner && state->owner->onClosed)
            state->owner->onClosed();
    };
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

    // The same rows as before change in place, so an open submenu stays open.
    State::Shown &shown = state->shown[id];
    const bool rebuild = ids != shown.rows || submenus != shown.submenus;
    shown.rows = ids;
    shown.submenus = submenus;
    if (rebuild)
        menu.clear();

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
                wire(state, *menu.addSubmenu(text), row->id);
                // Its rows ready for when it opens.
                if (!row->childrenLoaded)
                    model->refresh(row->id);
            } else {
                const int32_t rowId = row->id;
                menu.addItem(text, [state, rowId] {
                    if (state->model)
                        state->model->activate(rowId);
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
}

} // namespace cutefish::shell
