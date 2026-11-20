// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_VPN_NOTIFIER_H_
#define CUTEFISH_SHELL_MENUBAR_VPN_NOTIFIER_H_

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "shell/common/pending_calls.h"

namespace cutefish::shell {

class VpnNotifier
{
public:
    explicit VpnNotifier(base::dbus::Connection *bus);
    ~VpnNotifier();

    VpnNotifier(const VpnNotifier &) = delete;
    VpnNotifier &operator=(const VpnNotifier &) = delete;

    void vpnFailed(const std::string &name, const std::string &message);

    std::function<void()> openSettings;

private:
    base::dbus::Connection *m_bus;
    base::dbus::Proxy m_server;
    PendingCalls m_calls;
    std::vector<base::dbus::Match> m_matches;
    std::set<uint32_t> m_shown;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_VPN_NOTIFIER_H_
