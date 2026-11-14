// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session/free_console.h"

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "base/dbus.h"
#include "base/strings.h"

namespace cutefish::session {

std::set<int> displayManagerConsoles(const char *variable)
{
    std::set<int> consoles;
    if (!variable)
        return consoles;
    for (const std::string &part : base::strings::split(variable, ',')) {
        if (const std::optional<int> vt = base::strings::toNumber<int>(base::strings::trimmed(part));
            vt && *vt > 0)
            consoles.insert(*vt);
    }
    return consoles;
}

bool mayEnterConsole(int vt, const std::set<int> &displayManager, const std::set<int> &taken)
{
    return !displayManager.contains(vt) || taken.contains(vt);
}

int freeConsole(const std::set<int> &taken, const std::set<int> &displayManager)
{
    const std::set<int> reserved = displayManager.empty() ? std::set<int> {1, 2} : displayManager;
    for (int vt = 1; vt <= kAutoConsoles; ++vt) {
        if (!taken.contains(vt) && !reserved.contains(vt))
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
