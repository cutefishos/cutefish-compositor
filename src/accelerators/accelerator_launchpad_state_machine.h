// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/accelerators/accelerator_launcher_state_machine.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65
//
// Adapted for Cutefish: fed by the accelerator filter with keysyms and a
// pressed-key count instead of ui::KeyEvent and ui::InputController.

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_LAUNCHPAD_STATE_MACHINE_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_LAUNCHPAD_STATE_MACHINE_H_

#include <cstdint>
#include <functional>

namespace cutefish::accelerators {

// Decides when releasing Meta may open the launchpad: only when Meta went down
// with nothing else pressed and came back up with nothing else having been
// pressed or clicked in between.
class AcceleratorLaunchpadStateMachine
{
public:
    enum class LaunchpadState {
        // Waiting for the first input.
        Start,
        // The launchpad may be triggered. In any other state it is suppressed.
        Trigger,
        // Waiting solely for the release of Meta to move to Trigger.
        Primed,
        // Suppressed until every key is released again.
        Suppress,
    };

    // `anyKeysPressed` answers whether any key is currently held.
    explicit AcceleratorLaunchpadStateMachine(std::function<bool()> anyKeysPressed);
    AcceleratorLaunchpadStateMachine(const AcceleratorLaunchpadStateMachine &) = delete;
    AcceleratorLaunchpadStateMachine &operator=(const AcceleratorLaunchpadStateMachine &) = delete;
    ~AcceleratorLaunchpadStateMachine();

    void onKey(uint32_t keysym, bool pressed);
    void onPointerButton(bool pressed);

    bool canHandleLaunchpad() const { return m_state == LaunchpadState::Trigger; }

    LaunchpadState currentState() const { return m_state; }

private:
    LaunchpadState m_state = LaunchpadState::Start;
    std::function<bool()> m_anyKeysPressed;
};

} // namespace cutefish::accelerators

#endif // CUTEFISH_ACCELERATORS_ACCELERATOR_LAUNCHPAD_STATE_MACHINE_H_
