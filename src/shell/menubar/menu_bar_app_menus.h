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

struct MenuBarAppMenusHost {
    BarContext *bar = nullptr;
    MenuBar *menuBar = nullptr;
    std::function<void()> closePanels;
    std::function<void()> closeTrayMenu;
    // In the bar window's coordinates.
    std::function<ui::RectF()> availableGeometry;
};

class MenuBarAppMenus : public ui::View
{
public:
    virtual void closeMenu() = 0;
    virtual void barChanged() = 0;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MENU_BAR_APP_MENUS_H_
