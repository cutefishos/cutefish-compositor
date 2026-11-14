// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/accelerators/accelerator_launcher_state_machine.cc
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65
//
// Adapted for Cutefish: integrates the upstream behavior with native
// compositor types and ownership.

#include "accelerators/accelerator_launchpad_state_machine.h"

#include <utility>

#include <xkbcommon/xkbcommon-keysyms.h>

namespace cutefish::accelerators {

namespace {

bool isMetaKey(uint32_t keysym)
{
    return keysym == XKB_KEY_Super_L || keysym == XKB_KEY_Super_R;
}

bool isShiftKey(uint32_t keysym)
{
    return keysym == XKB_KEY_Shift_L || keysym == XKB_KEY_Shift_R;
}

} // namespace

AcceleratorLaunchpadStateMachine::AcceleratorLaunchpadStateMachine(
        std::function<bool()> anyKeysPressed)
    : m_anyKeysPressed(std::move(anyKeysPressed))
{
}

AcceleratorLaunchpadStateMachine::~AcceleratorLaunchpadStateMachine() = default;

void AcceleratorLaunchpadStateMachine::onKey(uint32_t keysym, bool pressed)
{
    switch (m_state) {
    // Trigger and Start share the same transitions: they differ only in
    // whether the launchpad may open right now.
    case LaunchpadState::Trigger:
    case LaunchpadState::Start:
        if (!pressed || isShiftKey(keysym)) {
            m_state = LaunchpadState::Start;
            break;
        }
        m_state = isMetaKey(keysym) ? LaunchpadState::Primed : LaunchpadState::Suppress;
        break;

    // Anything but Meta moves to Suppress; Meta released moves to Trigger.
    case LaunchpadState::Primed:
        if (!isMetaKey(keysym)) {
            m_state = LaunchpadState::Suppress;
            break;
        }
        if (!pressed)
            m_state = LaunchpadState::Trigger;
        break;

    // Back to Start once no key is held any more.
    case LaunchpadState::Suppress:
        if (!m_anyKeysPressed())
            m_state = LaunchpadState::Start;
        break;
    }
}

void AcceleratorLaunchpadStateMachine::onPointerButton(bool pressed)
{
    switch (m_state) {
    case LaunchpadState::Start:
    case LaunchpadState::Trigger:
    case LaunchpadState::Primed:
        if (pressed)
            m_state = LaunchpadState::Suppress;
        break;
    case LaunchpadState::Suppress:
        if (!pressed && !m_anyKeysPressed())
            m_state = LaunchpadState::Start;
        break;
    }
}

} // namespace cutefish::accelerators
