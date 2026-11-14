// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MENU_BAR_APP_MENUS_H_
#define CUTEFISH_SHELL_MENUBAR_MENU_BAR_APP_MENUS_H_

#include <functional>

#include "ui/geometry.h"
#include "ui/view.h"

namespace cutefish::shell {

class BarContext;
class MenuBar;

// What the bar gives the app menus it hosts.
struct MenuBarAppMenusHost {
    // The bar's look: lightContents(), menuTextColor(); entries are
    // BarItems with BarLabels (bar_item.h) on it.
    BarContext *bar = nullptr;
    // The backend: the AppMenu registrar, the power actions.
    MenuBar *menuBar = nullptr;
    // Closes the panels and the tray's menu, which opening a menu does first.
    std::function<void()> closePanels;
    std::function<void()> closeTrayMenu;
    // The work area in the bar window's coordinates; menus stay inside it.
    std::function<ui::RectF()> availableGeometry;
};

// The app menus' place in the bar: the first slot, filling the width the
// items on the right leave (from x 0, all 32 px of height). The bar clips it
// to that width; entries past it are cut, not elided.
class MenuBarAppMenus : public ui::View
{
public:
    // Part of closePopups(), when a press lands
    // between the bar's items, a panel or the tray opens, or a stop button
    // is clicked.
    virtual void closeMenu() = 0;
    // The bar's contents turned light or dark, or a panel opened or closed
    // (no hover plates while one is up).
    virtual void barChanged() = 0;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MENU_BAR_APP_MENUS_H_
