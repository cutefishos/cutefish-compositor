// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/wayland/wl_seat.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WAYLAND_PROTOCOLS_WL_SEAT_H_
#define CUTEFISH_WAYLAND_PROTOCOLS_WL_SEAT_H_

#include <cstdint>
#include <wayland-server-core.h>

#include "input/seat_observer.h"

namespace cutefish::input {
class Seat;
}

namespace cutefish::input {
class Pointer;
}

namespace cutefish::wayland {

input::Pointer* pointerFromResource(wl_resource* resource);

class SerialTracker;

// Version 8 for wl_pointer.axis_value120; version 9's axis_relative_direction needs the device's
// natural-scroll setting.
constexpr uint32_t kWlSeatVersion = 8;

// Owned by the caller, which must keep it alive for as long as the display.
struct WaylandSeat : input::SeatObserver {
    WaylandSeat(input::Seat* seat, SerialTracker* serialTracker);
    ~WaylandSeat() override;
    void onSurfaceFocused(compositor::Surface* gained, compositor::Surface* lost) override;
    WaylandSeat(const WaylandSeat&) = delete;
    WaylandSeat& operator=(const WaylandSeat&) = delete;

    input::Seat* const seat;
    SerialTracker* const serialTracker;
};

WaylandSeat* waylandSeatFromResource(wl_resource* resource);

wl_global* createSeatGlobal(wl_display* display, WaylandSeat* data);

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PROTOCOLS_WL_SEAT_H_
