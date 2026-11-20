// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_SENDER_FILTER_H_
#define CUTEFISH_SHELL_COMMON_SENDER_FILTER_H_

#include <string>

#include "base/dbus.h"

namespace cutefish::shell {

// sd-bus cannot match a well-known sender name, so rules with the same path and
// interface hear each other's signals.
class SenderFilter
{
public:
    SenderFilter(base::dbus::Connection *bus, const std::string &service)
    {
        if (service.starts_with(':')) {
            m_owner = service;
            return;
        }
        if (bus)
            m_watch = bus->watchName(service, [this](const std::string &owner) { m_owner = owner; });
    }

    SenderFilter(const SenderFilter &) = delete;
    SenderFilter &operator=(const SenderFilter &) = delete;

    bool accepts(const base::dbus::Message &message) const
    {
        return !m_owner.empty() && message.sender() == m_owner;
    }

private:
    std::string m_owner;
    base::dbus::NameWatch m_watch;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_SENDER_FILTER_H_
