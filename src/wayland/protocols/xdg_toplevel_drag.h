// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WAYLAND_PROTOCOLS_XDG_TOPLEVEL_DRAG_H_
#define CUTEFISH_WAYLAND_PROTOCOLS_XDG_TOPLEVEL_DRAG_H_

#include <wayland-server-core.h>

#include <cstdint>

namespace cutefish::wm {
class WindowManager;
}

namespace cutefish::wayland {

constexpr uint32_t kXdgToplevelDragManagerVersion = 1;

wl_global *createXdgToplevelDragGlobal(wl_display *display, wm::WindowManager *windowManager);

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PROTOCOLS_XDG_TOPLEVEL_DRAG_H_
