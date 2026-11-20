// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/wayland/xdg_shell.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WAYLAND_PROTOCOLS_XDG_SHELL_H_
#define CUTEFISH_WAYLAND_PROTOCOLS_XDG_SHELL_H_

#include <wayland-server-core.h>

#include <cstdint>
#include <functional>
#include <vector>

#include "base/geometry.h"

namespace cutefish::compositor {
class Compositor;
}

namespace cutefish::input {
class Seat;
}

namespace cutefish::wm {
class ShellSurface;
class WindowManager;
}

namespace cutefish::wayland {

class SerialTracker;

// Version 6: configure_bounds, wm_capabilities and the suspended state.
constexpr uint32_t kXdgWmBaseVersion = 6;

// How long a client has to answer xdg_wm_base.ping before its toplevels turn unresponsive.
constexpr int kXdgPingTimeoutMs = 5000;

class WaylandXdgShell
{
public:
    WaylandXdgShell(compositor::Compositor* compositor, wm::WindowManager* windowManager,
                    input::Seat* seat, SerialTracker* serialTracker);
    WaylandXdgShell(const WaylandXdgShell&) = delete;
    WaylandXdgShell& operator=(const WaylandXdgShell&) = delete;
    ~WaylandXdgShell();

    wm::WindowManager* windowManager() const { return m_windowManager; }
    input::Seat* seat() const { return m_seat; }
    SerialTracker* serialTracker() const { return m_serialTracker; }

    int pingTimeout() const { return m_pingTimeoutMs; }

    void setPingTimeout(int milliseconds) { m_pingTimeoutMs = milliseconds; }

    // Live xdg_surface resources, so xdg_wm_base.destroy can raise
    // defunct_surfaces.
    void addXdgSurface(wl_resource* resource);
    void removeXdgSurface(wl_resource* resource);
    bool hasXdgSurfaceForClient(wl_client* client) const;

private:
    wm::WindowManager* m_windowManager = nullptr;
    input::Seat* m_seat = nullptr;
    SerialTracker* m_serialTracker = nullptr;
    int m_pingTimeoutMs = kXdgPingTimeoutMs;
    std::vector<wl_resource*> m_xdgSurfaces;
};

wl_global* createXdgShellGlobal(wl_display* display, WaylandXdgShell* shell);

// Null when the xdg_surface it belongs to has already been destroyed.
wm::ShellSurface* shellSurfaceForToplevel(wl_resource* toplevelResource);

// Null when it has no xdg_toplevel role.
wm::ShellSurface* toplevelShellSurfaceForSurface(wl_resource* surfaceResource);

enum class ParentlessPopupResult {
    Adopted,
    // It has a parent already, or its xdg_surface is gone.
    HasParent,
    // There was nowhere to put it; popup_done went out.
    Dismissed,
};

// The popup is solved against `parent`'s output work area with its anchor moved by `anchorOffset`;
// a null parent, or `adopt` refusing, dismisses it.
ParentlessPopupResult adoptParentlessPopup(wl_resource* popupResource, wm::ShellSurface* parent,
                                           const base::Point& anchorOffset,
                                           const std::function<bool(wm::ShellSurface*, const base::Rect&)>& adopt);

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PROTOCOLS_XDG_SHELL_H_
