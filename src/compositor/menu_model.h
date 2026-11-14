// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_COMPOSITOR_MENU_MODEL_H_
#define CUTEFISH_COMPOSITOR_MENU_MODEL_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"
#include "ui/bitmap.h"

namespace cutefish::compositor {

// One entry of an application's menu.
struct MenuItem {
    enum class Toggle {
        None,
        Checkmark,
        Radio,
    };

    // What a submenu of a main menu is for.
    enum class Role {
        None,
        // The application's own menu, opened from its name.
        Application,
        Windows,
        Help,
        Services,
    };

    int32_t id = 0;
    int32_t parent = -1;
    bool separator = false;
    // With "_" mnemonic markers, "__" for a literal underscore.
    std::string label;
    bool enabled = true;
    bool visible = true;
    std::string iconName;
    // A picture of the application's own; null while there is none.
    std::shared_ptr<const ui::Bitmap> icon;
    // Each combination a list of keys, as "Control", "Shift", "q".
    std::vector<std::vector<std::string>> shortcut;
    Toggle toggle = Toggle::None;
    // 0 off, 1 on, anything else indeterminate.
    int32_t toggleState = -1;
    // Its entries come when it opens.
    bool hasSubmenu = false;
    // The role of the submenu the entry opens.
    Role role = Role::None;
    std::vector<int32_t> children;
    // A layout of its children arrived at least once.
    bool childrenLoaded = false;
};

// The label without its mnemonic markers.
inline std::string plainMenuLabel(const std::string &label)
{
    std::string text;
    text.reserve(label.size());
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] == '_') {
            if (i + 1 < label.size() && label[i + 1] == '_') {
                text += '_';
                ++i;
            }
            continue;
        }
        text += label[i];
    }
    return text;
}

// An application's menu as the menu bar shows it, from whichever channel
// the application uses: entries by id, the root being 0, each listing its
// children. Before a submenu shows the application is asked to bring it up
// to date; it says when its entries arrived and when one of them changed.
class MenuModel
{
public:
    virtual ~MenuModel() = default;

    // Null for an id not (or no longer) in the menu.
    virtual const MenuItem *item(int32_t id) const = 0;
    const MenuItem &root() const { return *item(0); }

    // Fetches the entries of `id` again.
    virtual void refresh(int32_t id) = 0;
    // The submenu `id` opens: prepare() and opened() at once.
    void aboutToShow(int32_t id)
    {
        prepare(id);
        opened(id);
    }
    // The submenu `id` is about to open: the application brings its entries
    // up to date, and menuUpdated follows.
    virtual void prepare(int32_t id) = 0;
    // The submenu `id` is on screen.
    virtual void opened(int32_t id) = 0;
    // The submenu `id` closed.
    virtual void closed(int32_t id) = 0;
    // The entry was chosen.
    virtual void activate(int32_t id) = 0;

    // The entries of `id` arrived, or a fetch failed; either way the menu
    // shows what it has.
    base::Signal<int32_t> menuUpdated;
    // Properties of an entry changed in place.
    base::Signal<int32_t> itemChanged;
    // The application asks for the entry to be shown, e.g. for its shortcut.
    base::Signal<int32_t> activationRequested;
};

} // namespace cutefish::compositor

#endif // CUTEFISH_COMPOSITOR_MENU_MODEL_H_
