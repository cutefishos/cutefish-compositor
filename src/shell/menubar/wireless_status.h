// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_WIRELESS_STATUS_H_
#define CUTEFISH_SHELL_MENUBAR_WIRELESS_STATUS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"
#include "shell/menubar/menu_bar_panels.h"

namespace cutefish::networkmanager {
class Client;
class Handler;
} // namespace cutefish::networkmanager

namespace cutefish::shell {

class WirelessStatus
{
public:
    WirelessStatus();
    ~WirelessStatus();

    WirelessStatus(const WirelessStatus &) = delete;
    WirelessStatus &operator=(const WirelessStatus &) = delete;

    networkmanager::Handler &handler() const { return *m_handler; }

    MenuBarIndicators::Network network() const;

    base::Signal<> changed;

private:
    bool ready() const;
    void updateActiveConnection();

    networkmanager::Client *m_client;
    std::unique_ptr<networkmanager::Handler> m_handler;
    std::string m_wirelessName;
    std::string m_wirelessIcon;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_WIRELESS_STATUS_H_
