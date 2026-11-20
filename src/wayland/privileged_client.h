// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WAYLAND_PRIVILEGED_CLIENT_H_
#define CUTEFISH_WAYLAND_PRIVILEGED_CLIENT_H_

#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <wayland-server-core.h>

namespace cutefish::wayland {

// Trusted is the wl_client made from the session's socketpair fd, never a client of the public socket.
// Private globals are filtered from other registries, and their bind handlers must check isTrusted() too.
class PrivilegedClients
{
public:
    explicit PrivilegedClients(wl_display *display);
    PrivilegedClients(const PrivilegedClients &) = delete;
    PrivilegedClients &operator=(const PrivilegedClients &) = delete;
    ~PrivilegedClients();
    void shutdown();

    // Takes ownership of `fd`. Returns null and closes `fd` on failure.
    wl_client *adoptShellConnection(int fd);

    wl_client *shellClient() const { return m_shell.client; }

    bool isTrusted(const wl_client *client) const { return client && client == m_shell.client; }

    // Returns the other end of a new capture socketpair for the caller to hand to the helper and close, or -1.
    int createCaptureConnection();
    bool isCaptureConnection(const wl_client *client) const;

    void addPrivateGlobal(wl_global *global);
    void addRestrictedGlobal(wl_global *global, std::function<bool(const wl_client *)> authorize);
    void removeRestrictedGlobal(wl_global *global);

private:
    struct TrustedConnection;

    // Standard layout, so libwayland's wl_listener* can be cast back to it.
    struct DestroyListener {
        wl_listener listener;
        TrustedConnection *connection;
    };

    struct TrustedConnection {
        wl_client *client = nullptr;
        const char *label = "";
        DestroyListener destroy {};
        bool listening = false;
    };

    static bool globalFilter(const wl_client *client, const wl_global *global, void *data);
    static void onConnectionDestroyed(wl_listener *listener, void *data);

    wl_client *adopt(TrustedConnection &connection, int fd);
    static void forget(TrustedConnection &connection);

    wl_display *m_display = nullptr;
    TrustedConnection m_shell {nullptr, "shell", {}, false};
    std::vector<std::unique_ptr<TrustedConnection>> m_captureConnections;
    std::vector<wl_global *> m_privateGlobals;
    std::map<const wl_global *, std::function<bool(const wl_client *)>> m_restrictedGlobals;
};

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PRIVILEGED_CLIENT_H_
