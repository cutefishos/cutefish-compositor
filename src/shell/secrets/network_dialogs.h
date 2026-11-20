// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SECRETS_NETWORK_DIALOGS_H_
#define CUTEFISH_SHELL_SECRETS_NETWORK_DIALOGS_H_

#include <string>
#include <vector>

#include "shell/secrets/network_secret_agent.h"
#include "shell/secrets/secret_dialog.h"

namespace cutefish::ui {
class CheckBox;
class TextField;
} // namespace cutefish::ui

namespace cutefish::shell {

// Connect stays off until the field holds a valid WEP key or WPA passphrase.
class WirelessPasswordDialog final : public SecretDialog
{
public:
    WirelessPasswordDialog(Shell *shell, NetworkSecretAgent *agent);

    // Reuses the window: an empty field, the password hidden again.
    void requestChanged();

    static std::string title(NetworkSecretAgent::Kind kind);
    static std::string text(const NetworkSecretAgent &agent);
    // WEP: 5 or 13 characters or 10 or 26 hex digits; WPA: 8 to 63
    // characters or 64 hex digits; anything else: not empty.
    static bool acceptable(NetworkSecretAgent::Kind kind, const std::string &text);
    static bool intermediate(NetworkSecretAgent::Kind kind, const std::string &text);

    ui::TextField *field() const { return m_field; }
    ui::CheckBox *showPassword() const { return m_show; }

protected:
    void build(ui::View *frame) override;
    ui::View *initialFocus() const override;
    void accept() override;
    void update() override;
    void retranslateRows() override;

private:
    NetworkSecretAgent *m_agent;
    ui::TextField *m_field = nullptr;
    ui::CheckBox *m_show = nullptr;
};

class VpnPasswordDialog final : public SecretDialog
{
public:
    VpnPasswordDialog(Shell *shell, NetworkSecretAgent *agent);

    void requestChanged();

    static std::string text(const NetworkSecretAgent &agent);

    const std::vector<ui::TextField *> &fields() const { return m_fields; }
    ui::CheckBox *showPassword() const { return m_show; }

protected:
    void build(ui::View *frame) override;
    ui::View *initialFocus() const override;
    void accept() override;
    void update() override;
    void retranslateRows() override;

private:
    void rebuildFields();
    bool complete() const;
    void applyEcho();

    NetworkSecretAgent *m_agent;
    ui::View *m_frame = nullptr;
    std::vector<NetworkSecretAgent::Field> m_fieldInfo;
    std::vector<ui::TextField *> m_fields;
    ui::CheckBox *m_show = nullptr;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SECRETS_NETWORK_DIALOGS_H_
