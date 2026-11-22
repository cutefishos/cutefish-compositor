// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RIPPLE_DISCOVERY_H_
#define CUTEFISH_RIPPLE_DISCOVERY_H_

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ripple/protocol.h"

namespace cutefish::ripple {

// The multicast side of LocalSend: 224.0.0.167:53317. Devices shout their
// identity; the rest answer with an HTTP /register, which also carries the
// device that answered.
class Discovery
{
public:
    using DeviceHandler = std::function<void(const Device &)>;

    Discovery();
    ~Discovery();

    Discovery(const Discovery &) = delete;
    Discovery &operator=(const Discovery &) = delete;

    // Binds the port and joins the group on every interface, the loopback
    // included so two instances on one computer find each other.
    bool start(int port, const std::string &ownFingerprint);
    void stop();

    int fd() const { return m_fd; }
    // Reads what arrived; called when the loop says the port is readable.
    void handleRead();

    // Sends the announcement that asks the others to answer.
    void announce(const Device &self);

    // Adds or refreshes a device heard from, or one that registered with us.
    void remember(const Device &device);
    // Drops devices not heard from for a while.
    void prune(std::chrono::seconds age = std::chrono::seconds(300));

    std::vector<Device> peers() const;
    std::string peersJson() const;

    // A device announced itself and wants an answer.
    void setOnAnnounce(DeviceHandler handler) { m_onAnnounce = std::move(handler); }
    void setOnChanged(std::function<void()> handler) { m_onChanged = std::move(handler); }

private:
    struct Entry
    {
        Device device;
        std::chrono::steady_clock::time_point seen;
    };

    int m_fd = -1;
    std::string m_fingerprint;
    mutable std::mutex m_mutex;
    std::map<std::string, Entry> m_peers;
    DeviceHandler m_onAnnounce;
    std::function<void()> m_onChanged;
};

} // namespace cutefish::ripple

#endif // CUTEFISH_RIPPLE_DISCOVERY_H_
