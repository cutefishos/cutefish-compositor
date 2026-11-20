// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_CORE_COMPUTER_CONTROL_ENVIRONMENT_H_
#define CUTEFISH_CORE_COMPUTER_CONTROL_ENVIRONMENT_H_

#include <functional>

#include "computer_control/controller.h"

namespace cutefish {

namespace computer_control {
class PermissionConsent;
}

namespace input {
class Seat;
}

namespace session {
class SessionController;
}

namespace wayland {
class InputMethodManager;
}

namespace wm {
class WindowManager;
}

namespace core {

struct ComputerControlHosts {
    wm::WindowManager *windowManager = nullptr;
    input::Seat *seat = nullptr;
    const session::SessionController *session = nullptr;
    computer_control::PermissionConsent *consent = nullptr;
    // Wayland comes up after the controller, so the input method is looked up late.
    std::function<wayland::InputMethodManager *()> inputMethod;
    // Whether the process `pid` shows system dialogs the agent must leave alone.
    std::function<bool(int64_t pid)> showsSystemDialogs;
    // Empty for none.
    std::function<std::string(uint64_t id)> windowApplication;
};

computer_control::Environment computerControlEnvironment(ComputerControlHosts hosts);

} // namespace core
} // namespace cutefish

#endif // CUTEFISH_CORE_COMPUTER_CONTROL_ENVIRONMENT_H_
