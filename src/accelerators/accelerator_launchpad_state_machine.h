// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/accelerators/accelerator_launcher_state_machine.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_LAUNCHPAD_STATE_MACHINE_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_LAUNCHPAD_STATE_MACHINE_H_

#include <cstdint>
#include <functional>

namespace cutefish::accelerators {

// Releasing Meta may open the launchpad only when nothing else was pressed or
// clicked while it was down.
class AcceleratorLaunchpadStateMachine
{
public:
    enum class LaunchpadState {
        Start,
        // The launchpad may be triggered. In any other state it is suppressed.
        Trigger,
        Primed,
        Suppress,
    };

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
