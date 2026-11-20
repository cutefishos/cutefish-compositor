// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/wayland/server.h
//           chromium/src/components/exo/wayland/wayland_watcher.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WAYLAND_SERVER_H_
#define CUTEFISH_WAYLAND_SERVER_H_

#include <chrono>
#include <memory>
#include <map>
#include <set>
#include <string>

#include "client_tracker.h"
#include "scoped_wl.h"
#include "serial_tracker.h"

namespace cutefish::wayland {

class Server
{
public:
    Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    ~Server();

    // Null when no Server owns `display`.
    static Server* serverForDisplay(wl_display* display);

    // An empty `name` picks a free `wayland-N`. Must be called before start().
    bool addSocket(const std::string& name = {});

    // Clients are flushed after every dispatch and before the loop blocks.
    bool start();

    // Detaches from the event loop; the display stays alive.
    void stop();

    // Empty until addSocket() succeeds.
    const std::string& socketName() const { return m_socketName; }

    wl_display* display() const { return m_display.get(); }

    // The event loop fd, or -1 if there is no display.
    int fileDescriptor() const;

    // Dispatches pending events, blocking for at most `timeout`. A zero
    // timeout polls. Returns false on a dispatch error.
    bool dispatch(std::chrono::milliseconds timeout);

    void flush();

    bool isClientDestroyed(wl_client* client) const;

    // Compatibility frontends publish their surfaces only after role association.
    void setClientSurfacesInitiallyHidden(wl_client* client, bool hidden)
    {
        if (hidden)
            m_hiddenSurfaceClients.insert(client);
        else
            m_hiddenSurfaceClients.erase(client);
    }
    bool clientSurfacesInitiallyHidden(wl_client* client) const
    {
        return m_hiddenSurfaceClients.contains(client);
    }

    // Client surface units per logical unit: surface coordinates from the client are divided by it,
    // and those it receives multiplied.
    void setClientScale(wl_client* client, double scale)
    {
        if (scale == 1)
            m_clientScales.erase(client);
        else
            m_clientScales[client] = scale;
    }
    double clientScale(wl_client* client) const
    {
        const auto it = m_clientScales.find(client);
        return it == m_clientScales.end() ? 1 : it->second;
    }
    // The client's cursor image pixels per logical unit, where they differ
    // from its surface units; 0 drops it.
    void setCursorScale(wl_client* client, double scale)
    {
        if (scale <= 0)
            m_cursorScales.erase(client);
        else
            m_cursorScales[client] = scale;
    }
    double cursorScale(wl_client* client) const
    {
        const auto it = m_cursorScales.find(client);
        return it == m_cursorScales.end() ? clientScale(client) : it->second;
    }

    SerialTracker* serialTracker() { return m_serialTracker.get(); }
    int trackedClientCount() const { return m_clientTracker->trackedClientCount(); }

private:
    struct LoopHook;

    void onSocketActivated();

    // Declaration order is destruction order reversed, and both matter here:
    // ClientTracker must unregister its wl_listeners while the display is still
    // alive, while destroying the display tears down clients and so must still
    // see a live SerialTracker.
    std::unique_ptr<SerialTracker> m_serialTracker;
    ScopedWlDisplay m_display;
    std::unique_ptr<ClientTracker> m_clientTracker;

    std::set<wl_client*> m_hiddenSurfaceClients;
    std::map<wl_client*, double> m_clientScales;
    std::map<wl_client*, double> m_cursorScales;
    std::unique_ptr<LoopHook> m_loopHook;
    std::string m_socketName;
};

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_SERVER_H_
