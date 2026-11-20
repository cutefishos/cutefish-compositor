// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_COMPUTER_CONTROL_PERMISSION_CONSENT_H_
#define CUTEFISH_COMPUTER_CONTROL_PERMISSION_CONSENT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "base/dbus.h"
#include "computer_control/controller.h"
#include "computer_control/remembered_apps.h"

namespace cutefish::computer_control {

// Any session client can own a bus name first, so the owner's process must show
// system dialogs, and the prompt goes to the owner's unique name.
class PermissionConsent
{
public:
    // The session bus when no connection is given.
    explicit PermissionConsent(std::function<bool(int64_t pid)> showsSystemDialogs,
                               RememberedApps *remembered = nullptr,
                               base::dbus::Connection *bus = nullptr);
    PermissionConsent(const PermissionConsent &) = delete;
    PermissionConsent &operator=(const PermissionConsent &) = delete;
    ~PermissionConsent();

    // Must outlive the questions it asks. `appId` is empty when the agent has none.
    std::function<void()> ask(const std::string &agentName, int64_t pid, const std::string &appId,
                              std::function<void(ConsentResult)> answer);

private:
    struct Request;
    void prompt(const std::shared_ptr<Request> &request, const std::string &owner);

    std::function<bool(int64_t)> m_showsSystemDialogs;
    RememberedApps *m_remembered;
    base::dbus::Connection *m_bus;
};

} // namespace cutefish::computer_control

#endif // CUTEFISH_COMPUTER_CONTROL_PERMISSION_CONSENT_H_
