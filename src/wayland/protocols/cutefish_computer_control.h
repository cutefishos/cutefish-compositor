// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WAYLAND_PROTOCOLS_CUTEFISH_COMPUTER_CONTROL_H_
#define CUTEFISH_WAYLAND_PROTOCOLS_CUTEFISH_COMPUTER_CONTROL_H_

#include <functional>

#include <wayland-server-core.h>

namespace cutefish::computer_control {
class Controller;
}

namespace cutefish::wayland {

class WaylandComputerControl
{
public:
    // Only clients `authorize` accepts may bind the global; the registry filter
    // hides it from the rest.
    WaylandComputerControl(wl_display *display, computer_control::Controller *controller,
                           std::function<bool(const wl_client *)> authorize);
    ~WaylandComputerControl();

    WaylandComputerControl(const WaylandComputerControl &) = delete;
    WaylandComputerControl &operator=(const WaylandComputerControl &) = delete;

    wl_global *global() const { return m_global; }

private:
    static void bind(wl_client *client, void *data, uint32_t version, uint32_t id);

    computer_control::Controller *m_controller;
    std::function<bool(const wl_client *)> m_authorize;
    wl_global *m_global = nullptr;
};

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PROTOCOLS_CUTEFISH_COMPUTER_CONTROL_H_
