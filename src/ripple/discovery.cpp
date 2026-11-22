// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ripple/discovery.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

#include "base/json.h"

namespace cutefish::ripple {

namespace {

constexpr const char *kGroup = "224.0.0.167";

std::string addressOf(const sockaddr_in &address)
{
    char text[INET_ADDRSTRLEN] = {};
    if (!inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text)))
        return {};
    return text;
}

} // namespace

Discovery::Discovery() = default;

Discovery::~Discovery()
{
    stop();
}

bool Discovery::start(int port, const std::string &ownFingerprint)
{
    m_fingerprint = ownFingerprint;
    m_fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (m_fd < 0)
        return false;

    int reuse = 1;
    ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(uint16_t(port));
    if (::bind(m_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        stop();
        return false;
    }

    // Every interface, the loopback included, so a second instance on this
    // computer is found as well.
    ip_mreq request {};
    inet_pton(AF_INET, kGroup, &request.imr_multiaddr);
    request.imr_interface.s_addr = htonl(INADDR_ANY);
    ::setsockopt(m_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request));

    ifaddrs *interfaces = nullptr;
    if (getifaddrs(&interfaces) == 0) {
        for (ifaddrs *entry = interfaces; entry; entry = entry->ifa_next) {
            if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET)
                continue;
            if (!(entry->ifa_flags & IFF_UP))
                continue;
            ip_mreq membership {};
            membership.imr_multiaddr = request.imr_multiaddr;
            membership.imr_interface = reinterpret_cast<sockaddr_in *>(entry->ifa_addr)->sin_addr;
            ::setsockopt(m_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership));
        }
        freeifaddrs(interfaces);
    }

    unsigned char loop = 1;
    ::setsockopt(m_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    unsigned char ttl = 1;
    ::setsockopt(m_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    return true;
}

void Discovery::stop()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

void Discovery::announce(const Device &self)
{
    if (m_fd < 0)
        return;
    const std::string text = self.toJson(true).serialize();
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(uint16_t(self.port));
    inet_pton(AF_INET, kGroup, &address.sin_addr);

    // Once per interface's address, so a host with Wi-Fi and Ethernet reaches
    // both networks.
    ifaddrs *interfaces = nullptr;
    int sent = 0;
    if (getifaddrs(&interfaces) == 0) {
        for (ifaddrs *entry = interfaces; entry; entry = entry->ifa_next) {
            if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET)
                continue;
            if (!(entry->ifa_flags & IFF_UP) || (entry->ifa_flags & IFF_LOOPBACK)
                || (entry->ifa_flags & IFF_POINTOPOINT))
                continue;
            in_addr local = reinterpret_cast<sockaddr_in *>(entry->ifa_addr)->sin_addr;
            ::setsockopt(m_fd, IPPROTO_IP, IP_MULTICAST_IF, &local, sizeof(local));
            if (::sendto(m_fd, text.data(), text.size(), 0, reinterpret_cast<sockaddr *>(&address),
                         sizeof(address)) > 0)
                ++sent;
        }
        freeifaddrs(interfaces);
    }
    if (sent > 0)
        return;

    // No interface carried it; the default route still may.
    const in_addr any {};
    ::setsockopt(m_fd, IPPROTO_IP, IP_MULTICAST_IF, &any, sizeof(any));
    ::sendto(m_fd, text.data(), text.size(), 0, reinterpret_cast<sockaddr *>(&address), sizeof(address));
}

void Discovery::handleRead()
{
    char buffer[65536];
    sockaddr_in from {};
    socklen_t length = sizeof(from);
    const ssize_t got = ::recvfrom(m_fd, buffer, sizeof(buffer) - 1, 0,
                                   reinterpret_cast<sockaddr *>(&from), &length);
    if (got <= 0)
        return;
    buffer[got] = '\0';

    const auto parsed = base::json::Value::parse(std::string_view(buffer, size_t(got)));
    if (!parsed || !parsed->isObject())
        return;
    std::optional<Device> device = Device::fromJson(*parsed);
    if (!device || device->fingerprint == m_fingerprint)
        return;

    device->host = addressOf(from);
    const bool announce = (*parsed)["announce"].toBool();
    remember(*device);
    if (announce && m_onAnnounce)
        m_onAnnounce(*device);
}

void Discovery::remember(const Device &device)
{
    if (device.fingerprint.empty() || device.fingerprint == m_fingerprint)
        return;
    bool changed = false;
    {
        const std::lock_guard lock(m_mutex);
        auto it = m_peers.find(device.fingerprint);
        if (it == m_peers.end()) {
            m_peers[device.fingerprint] = Entry {device, std::chrono::steady_clock::now()};
            changed = true;
        } else {
            Device &known = it->second.device;
            if (known.host != device.host || known.port != device.port || known.alias != device.alias
                || known.protocol != device.protocol || known.model != device.model
                || known.type != device.type) {
                known = device;
                changed = true;
            } else if (!device.host.empty()) {
                known.host = device.host;
            }
            it->second.seen = std::chrono::steady_clock::now();
        }
    }
    if (changed && m_onChanged)
        m_onChanged();
}

void Discovery::prune(std::chrono::seconds age)
{
    const auto now = std::chrono::steady_clock::now();
    bool changed = false;
    {
        const std::lock_guard lock(m_mutex);
        for (auto it = m_peers.begin(); it != m_peers.end();) {
            if (now - it->second.seen > age) {
                it = m_peers.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    if (changed && m_onChanged)
        m_onChanged();
}

std::vector<Device> Discovery::peers() const
{
    const std::lock_guard lock(m_mutex);
    std::vector<Device> devices;
    devices.reserve(m_peers.size());
    for (const auto &[fingerprint, entry] : m_peers)
        devices.push_back(entry.device);
    return devices;
}

std::string Discovery::peersJson() const
{
    base::json::Value::Array array;
    {
        const std::lock_guard lock(m_mutex);
        for (const auto &[fingerprint, entry] : m_peers) {
            base::json::Value value = entry.device.toJson();
            value.insert("host", entry.device.host);
            array.push_back(std::move(value));
        }
    }
    return base::json::Value(std::move(array)).serialize();
}

} // namespace cutefish::ripple
