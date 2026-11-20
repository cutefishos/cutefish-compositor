// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/wayland/server_util.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WAYLAND_SERVER_UTIL_H_
#define CUTEFISH_WAYLAND_SERVER_UTIL_H_

#include <wayland-server-core.h>

#include <chrono>
#include <cstdint>
#include <memory>

#include "base/geometry.h"

namespace cutefish::compositor {
class Surface;
}

namespace cutefish::wayland {

// Registered when the wl_surface is created and cleared when the resource dies.
compositor::Surface* surfaceFromResource(wl_resource* resource);
wl_resource* getSurfaceResource(compositor::Surface* surface);
void setSurfaceResource(compositor::Surface* surface, wl_resource* resource);

template<class T>
T* getUserDataAs(wl_resource* resource)
{
    return static_cast<T*>(wl_resource_get_user_data(resource));
}

template<class T>
std::unique_ptr<T> takeUserDataAs(wl_resource* resource)
{
    std::unique_ptr<T> userData(getUserDataAs<T>(resource));
    wl_resource_set_user_data(resource, nullptr);
    return userData;
}

template<class T>
void destroyUserData(wl_resource* resource)
{
    takeUserDataAs<T>(resource);
}

template<class T>
void setImplementation(wl_resource* resource, const void* implementation,
                       std::unique_ptr<T> userData)
{
    wl_resource_set_implementation(resource, implementation, userData.release(),
                                   destroyUserData<T>);
}

void setImplementation(wl_resource* resource, const void* implementation);

// Truncated to uint32_t, so it wraps.
uint32_t timePointToMilliseconds(std::chrono::steady_clock::time_point tp);
uint32_t nowInMilliseconds();

// Client resources start to be freed once destruction begins, so they must not be queried after.
bool isClientDestroyed(wl_client* client);

// 1 for every client but Xwayland.
double clientScale(wl_client* client);
double cursorScale(wl_client* client);

// Every edge stays within 2^24 of zero, so no sum the compositor makes of them overflows. A negative
// size is empty.
base::Rect clientRect(int32_t x, int32_t y, int32_t width, int32_t height);

// Surface coordinates from a scaled client, in logical units. Rectangles grow
// outwards, so damage and regions never lose a partly covered pixel.
base::Rect clientToLogical(const base::Rect& rect, double scale);
base::Point clientToLogical(const base::Point& point, double scale);

// Logical surface coordinates in a scaled client's units.
base::PointF logicalToClient(const base::PointF& point, double scale);

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_SERVER_UTIL_H_
