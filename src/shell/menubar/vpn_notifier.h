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

// A VPN that went down on its own is told
// with a notification, and a click on it opens its page in Settings.
class VpnNotifier
{
public:
    explicit VpnNotifier(base::dbus::Connection *bus);
    ~VpnNotifier();

    VpnNotifier(const VpnNotifier &) = delete;
    VpnNotifier &operator=(const VpnNotifier &) = delete;

    // NetworkManager's Handler::vpnFailed.
    void vpnFailed(const std::string &name, const std::string &message);

    // What a click does: the owner opens Settings' VPN page.
    std::function<void()> openSettings;

private:
    base::dbus::Connection *m_bus;
    base::dbus::Proxy m_server;
    PendingCalls m_calls;
    std::vector<base::dbus::Match> m_matches;
    // The notifications shown and not yet closed.
    std::set<uint32_t> m_shown;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_VPN_NOTIFIER_H_
