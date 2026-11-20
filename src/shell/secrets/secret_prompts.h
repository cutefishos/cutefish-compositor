// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SECRETS_SECRET_PROMPTS_H_
#define CUTEFISH_SHELL_SECRETS_SECRET_PROMPTS_H_

#include <memory>

#include "base/signal.h"
#include "shell/shell.h"

namespace cutefish::base::dbus {
class Connection;
}

namespace cutefish::bluez {
class Manager;
}

namespace cutefish::networkmanager {
class Client;
}

namespace cutefish::shell {

class BluetoothPairingDialog;
class NetworkSecretAgent;
class PairingAgent;
class VpnPasswordDialog;
class WirelessPasswordDialog;

class SecretPrompts final : public Component
{
public:
    // Any may be null.
    SecretPrompts(Shell *shell, networkmanager::Client *network, base::dbus::Connection *session,
                  std::shared_ptr<bluez::Manager> bluetooth);
    static std::unique_ptr<SecretPrompts> create(Shell *shell);
    ~SecretPrompts() override;

    // Not for a development instance, which would take the session's prompts.
    void start();

    NetworkSecretAgent *networkAgent() const { return m_agent.get(); }
    WirelessPasswordDialog *wirelessDialog() const { return m_wireless.get(); }
    VpnPasswordDialog *vpnDialog() const { return m_vpn.get(); }
    BluetoothPairingDialog *pairingDialog() const { return m_pairing.get(); }

private:
    void networkRequestChanged();
    void pairingChanged();
    void retranslate();

    Shell *m_shell;
    networkmanager::Client *m_network;
    base::dbus::Connection *m_session;
    std::shared_ptr<bluez::Manager> m_bluetooth;
    std::unique_ptr<NetworkSecretAgent> m_agent;
    std::unique_ptr<WirelessPasswordDialog> m_wireless;
    std::unique_ptr<VpnPasswordDialog> m_vpn;
    std::unique_ptr<PairingAgent> m_pairingAgent;
    std::unique_ptr<BluetoothPairingDialog> m_pairing;
    base::Subscription m_requestSubscription;
    base::Subscription m_pairingSubscription;
    base::Subscription m_errorSubscription;
    base::Subscription m_languageChanged;
    bool m_systemServices = false;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SECRETS_SECRET_PROMPTS_H_
