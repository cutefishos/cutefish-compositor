// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WAYLAND_MAIN_MENU_H
#define CUTEFISH_WAYLAND_MAIN_MENU_H

#include <wayland-server-core.h>

namespace cutefish::wayland {

struct WaylandSeat;

// cutefish_main_menu_manager_v1: a window's main menu becomes its surface's
// compositor::MenuModel, which the status bar shows. A choice counts as
// input on the seat.
wl_global *createMainMenuGlobal(wl_display *display, WaylandSeat *seat);

} // namespace cutefish::wayland

#endif
