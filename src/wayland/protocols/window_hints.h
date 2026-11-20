// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WAYLAND_WINDOW_HINTS_H
#define CUTEFISH_WAYLAND_WINDOW_HINTS_H

#include <wayland-server-core.h>

namespace cutefish::wm {
class WindowManager;
}

namespace cutefish::wayland {

// With `windows`, managers hear the work areas of its outputs.
wl_global *createWindowHintsGlobal(wl_display *display, wm::WindowManager *windows = nullptr);

void sendInteractiveResize(wl_resource *surface, bool active);

} // namespace cutefish::wayland

#endif
