// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/wayland/wl_compositor.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WAYLAND_PROTOCOLS_WL_COMPOSITOR_H_
#define CUTEFISH_WAYLAND_PROTOCOLS_WL_COMPOSITOR_H_

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <cstdint>

namespace cutefish::compositor {
class Compositor;
}

namespace cutefish::wayland {

// Version 4, for wl_surface.damage_buffer; wl_surface.offset of version 5 is not implemented.
constexpr uint32_t kWlCompositorVersion = WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION;

wl_global* createCompositorGlobal(wl_display* display, compositor::Compositor* compositor);

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PROTOCOLS_WL_COMPOSITOR_H_
