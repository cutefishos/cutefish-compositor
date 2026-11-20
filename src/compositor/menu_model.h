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

struct MenuItem {
    enum class Toggle {
        None,
        Checkmark,
        Radio,
    };

    enum class Role {
        None,
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
    Role role = Role::None;
    std::vector<int32_t> children;
    bool childrenLoaded = false;
};

// Where a window's org.gtk.Menus menus and the org.gtk.Actions groups they
// activate are on the session bus.
struct GtkMenuAddress {
    std::string busName;
    std::string menubarPath;
    std::string appMenuPath;
    // "app." actions.
    std::string applicationPath;
    // "win." actions.
    std::string windowPath;

    bool hasMenus() const { return !busName.empty() && (!menubarPath.empty() || !appMenuPath.empty()); }
    bool operator==(const GtkMenuAddress &) const = default;
};

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

class MenuModel
{
public:
    virtual ~MenuModel() = default;

    // Null for an id not (or no longer) in the menu.
    virtual const MenuItem *item(int32_t id) const = 0;
    const MenuItem &root() const { return *item(0); }

    virtual void refresh(int32_t id) = 0;
    void aboutToShow(int32_t id)
    {
        prepare(id);
        opened(id);
    }
    // The application brings its entries up to date, and menuUpdated follows.
    virtual void prepare(int32_t id) = 0;
    virtual void opened(int32_t id) = 0;
    virtual void closed(int32_t id) = 0;
    virtual void activate(int32_t id) = 0;

    // Also emitted when a fetch failed.
    base::Signal<int32_t> menuUpdated;
    base::Signal<int32_t> itemChanged;
    base::Signal<int32_t> activationRequested;
};

} // namespace cutefish::compositor

#endif // CUTEFISH_COMPOSITOR_MENU_MODEL_H_
