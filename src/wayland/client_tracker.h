// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/wayland/client_tracker.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WAYLAND_CLIENT_TRACKER_H_
#define CUTEFISH_WAYLAND_CLIENT_TRACKER_H_

#include <wayland-server-core.h>

#include <memory>
#include <unordered_map>

namespace cutefish::wayland {

// Resources of a wl_client are freed while it is destroyed, so they must not be accessed once that begins.
class ClientTracker
{
public:
    explicit ClientTracker(wl_display* display);
    ClientTracker(const ClientTracker&) = delete;
    ClientTracker& operator=(const ClientTracker&) = delete;
    ~ClientTracker();

    // Client resources must not be queried once this is true.
    bool isClientDestroyed(wl_client* client) const;

    int trackedClientCount() const;

private:
    // Called when a client is created, and when it begins destruction, before its resources are freed.
    struct ClientListener {
        ClientListener(ClientTracker* tracker, wl_notify_func_t notify);
        wl_listener listener;
        ClientTracker* tracker;
    };

    static void onClientCreated(wl_listener* listener, void* data);
    static void onClientDestroyed(wl_listener* listener, void* data);

    void handleClientCreated(wl_client* client);
    void handleClientDestroyed(wl_client* client);

    wl_display* m_display;

    ClientListener m_clientCreatedListener;

    // Presence in `m_clientDestroyedListeners` indicates the client has been
    // created and has yet to be destroyed.
    std::unordered_map<wl_client*, std::unique_ptr<ClientListener>> m_clientDestroyedListeners;
};

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_CLIENT_TRACKER_H_
