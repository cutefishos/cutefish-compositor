// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/wayland/wl_shm.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WAYLAND_PROTOCOLS_WL_SHM_H_
#define CUTEFISH_WAYLAND_PROTOCOLS_WL_SHM_H_

#include <wayland-server-core.h>

#include <cstdint>
#include <memory>

namespace cutefish::compositor {
class Buffer;
class Compositor;
}

namespace cutefish::wayland {

constexpr uint32_t kWlShmVersion = 1;

// A wl_buffer resource owns its Buffer; surfaces only hold weak references, so
// that a client destroying wl_buffer while it is committed cannot dangle.
using BufferRef = std::shared_ptr<compositor::Buffer>;

// Null for a null resource.
BufferRef bufferFromResource(wl_resource* resource);

// Also clears the buffer's release callback when the resource dies, since a Buffer can outlive its wl_buffer.
void setBufferImplementation(wl_resource* resource, const void* implementation, BufferRef buffer);

wl_global* createShmGlobal(wl_display* display, compositor::Compositor* compositor);

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PROTOCOLS_WL_SHM_H_
