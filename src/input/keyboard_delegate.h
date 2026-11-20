// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/keyboard_delegate.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_KEYBOARD_DELEGATE_H_
#define CUTEFISH_INPUT_KEYBOARD_DELEGATE_H_

#include <chrono>
#include <map>
#include <string>

#include "input/types.h"

namespace cutefish::compositor {
class Surface;
}

namespace cutefish::input {

class KeyboardDelegate
{
public:
    virtual ~KeyboardDelegate() = default;

    virtual bool canAcceptKeyboardEventsForSurface(compositor::Surface* surface) const = 0;

    // Keyed by physical evdev code.
    using PressedKeys = std::map<uint32_t, KeyState>;

    virtual void onKeyboardEnter(compositor::Surface* surface,
                                 const PressedKeys& pressedKeys) = 0;
    virtual void onKeyboardLeave(compositor::Surface* surface) = 0;

    // Returns the serial the client will use to acknowledge the change.
    virtual uint32_t onKeyboardKey(Timestamp timestamp, uint32_t evdevCode, bool pressed) = 0;

    virtual void onKeyboardModifiers(const KeyboardModifiers& modifiers) = 0;

    virtual void onKeyRepeatSettingsChanged(bool enabled, std::chrono::milliseconds delay,
                                            std::chrono::milliseconds interval) = 0;

    virtual void onKeyboardLayoutUpdated(const std::string& keymap) = 0;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_KEYBOARD_DELEGATE_H_
