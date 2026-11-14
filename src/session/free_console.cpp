// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session/free_console.h"

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "base/dbus.h"

namespace cutefish::session {

int freeConsole(const std::set<int> &taken)
{
    for (int vt = 3; vt <= kAutoConsoles; ++vt) {
        if (!taken.contains(vt))
            return vt;
    }
    return 0;
}

std::set<int> takenConsoles(base::dbus::Connection *bus)
{
    std::set<int> taken;
    const base::dbus::Proxy manager(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                                    "org.freedesktop.login1.Manager");
    std::vector<std::tuple<std::string, uint32_t, std::string, std::string, base::dbus::ObjectPath>>
            sessions;
    if (!manager.call("ListSessions").read(sessions))
        return taken;
    for (const auto &session : sessions) {
        const base::dbus::Proxy proxy(bus, "org.freedesktop.login1", std::get<4>(session).path,
                                      "org.freedesktop.login1.Session");
        if (const std::optional<uint32_t> vt = proxy.property<uint32_t>("VTNr"); vt && *vt > 0)
            taken.insert(int(*vt));
    }
    return taken;
}

} // namespace cutefish::session
