// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_SESSION_SERVICES_H_
#define CUTEFISH_SHELL_CONTROLCENTER_SESSION_SERVICES_H_

#include <memory>

#include "shell/controlcenter/control_center_services.h"

namespace cutefish::shell {

class NotificationSettings;
class Shell;
class MenuBar;

// `notifications` may be null.
std::unique_ptr<ControlCenterServices> createSessionServices(Shell *shell, MenuBar &menuBar,
                                                             NotificationSettings *notifications);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_SESSION_SERVICES_H_
