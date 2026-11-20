// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wayland-server-core.h>

namespace cutefish::input {
class Seat;
}

namespace cutefish::wayland {
class PrivilegedClients;
}

namespace cutefish::xwayland {

// For Xwayland alone: an X11 client that grabs the keyboard inhibits the compositor's shortcuts while
// its window has focus.
class XwaylandKeyboardGrab
{
public:
    XwaylandKeyboardGrab(wl_display* display, wayland::PrivilegedClients* privileged,
                         wl_client* client, input::Seat* seat);
    ~XwaylandKeyboardGrab();

private:
    static void bind(wl_client*, void*, uint32_t, uint32_t);
    wl_global* m_global = nullptr;
    wayland::PrivilegedClients* m_privileged;
    wl_client* m_client;
    input::Seat* m_seat;
};

} // namespace cutefish::xwayland
