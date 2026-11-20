// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WAYLAND_STATUS_ITEM_H
#define CUTEFISH_WAYLAND_STATUS_ITEM_H

#include <wayland-server-core.h>

namespace cutefish::wm {
class StatusItems;
}

namespace cutefish::wayland {

wl_global *createStatusItemGlobal(wl_display *display, wm::StatusItems *items);

} // namespace cutefish::wayland

#endif
