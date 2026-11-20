// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SECRETS_BLUETOOTH_PAIRING_DIALOG_H_
#define CUTEFISH_SHELL_SECRETS_BLUETOOTH_PAIRING_DIALOG_H_

#include <memory>
#include <string>

#include "shell/secrets/secret_dialog.h"

namespace cutefish::ui {
class Label;
class TextField;
} // namespace cutefish::ui

namespace cutefish::shell {

class PairingAgent
{
public:
    virtual ~PairingAgent() = default;

    // Agent1's method that asked, e.g. "RequestConfirmation"; empty for none.
    virtual std::string pairingKind() const = 0;
    virtual std::string pairingDevice() const = 0;
    virtual std::string pairingCode() const = 0;
    virtual int pairingEntered() const = 0;
    virtual std::string errorText() const = 0;
    virtual void respondPairing(bool accept, const std::string &code) = 0;
};

// Display-only requests have no primary button.
class BluetoothPairingDialog final : public SecretDialog
{
public:
    BluetoothPairingDialog(Shell *shell, PairingAgent *agent);

    static std::string text(const std::string &kind, const std::string &device);

    ui::TextField *codeField() const { return m_field; }
    ui::Label *code() const { return m_code; }
    ui::Label *error() const { return m_error; }

protected:
    void build(ui::View *frame) override;
    ui::View *initialFocus() const override;
    void accept() override;
    void update() override;

private:
    bool inputRequired() const;
    bool displayOnly() const;

    PairingAgent *m_agent;
    ui::Label *m_code = nullptr;
    ui::Label *m_entered = nullptr;
    ui::TextField *m_field = nullptr;
    ui::Label *m_error = nullptr;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SECRETS_BLUETOOTH_PAIRING_DIALOG_H_
