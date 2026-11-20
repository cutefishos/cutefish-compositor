// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_SERVICE_OWNER_H_
#define CUTEFISH_SHELL_COMMON_SERVICE_OWNER_H_

#include <functional>
#include <string>
#include <utility>

#include "base/dbus.h"

namespace cutefish::shell {

// The session's services are not D-Bus activatable and start after the
// compositor: `appeared` runs each time `name` gets an owner.
inline base::dbus::NameWatch followServiceOwner(base::dbus::Connection *bus, const std::string &name,
                                                std::function<void()> appeared)
{
    if (!bus)
        return {};
    return bus->watchName(name, [appeared = std::move(appeared)](const std::string &owner) {
        if (!owner.empty())
            appeared();
    });
}

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_SERVICE_OWNER_H_
