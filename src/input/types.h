// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/key_state.h
//           chromium/src/components/exo/keyboard_modifiers.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_TYPES_H_
#define CUTEFISH_INPUT_TYPES_H_

#include <chrono>
#include <cstdint>
#include <tuple>

#include "base/geometry.h"

namespace cutefish::input {

// libinput timestamps come from CLOCK_MONOTONIC, which is what steady_clock
// reads on Linux, so the two are directly comparable.
using Timestamp = std::chrono::steady_clock::time_point;

// One wheel click in InputManager::onPointerAxis offsets, which are Wayland
// axis units.
constexpr double kAxisUnitsPerClick = 10.0;

enum ButtonFlag : uint32_t {
    NoButton = 0,
    LeftButton = 1u << 0,
    RightButton = 1u << 1,
    MiddleButton = 1u << 2,
    ForwardButton = 1u << 3,
    BackButton = 1u << 4,
};

using ButtonFlags = uint32_t;

// Maps an evdev BTN_* code to its flag, or NoButton for a button the pointer
// protocol has no place for.
ButtonFlags buttonFlagFromEvdev(uint32_t code);

// A keyboard's lock lights. The values are libinput_led's.
namespace KeyboardLed {
constexpr uint32_t NumLock = 1 << 0;
constexpr uint32_t CapsLock = 1 << 1;
constexpr uint32_t ScrollLock = 1 << 2;
} // namespace KeyboardLed

// The serialized xkb modifier state, sent verbatim as wl_keyboard.modifiers.
struct KeyboardModifiers {
    uint32_t depressed = 0;
    uint32_t locked = 0;
    uint32_t latched = 0;
    uint32_t group = 0;
};

inline bool operator==(const KeyboardModifiers& lhs, const KeyboardModifiers& rhs)
{
    return std::tie(lhs.depressed, lhs.locked, lhs.latched, lhs.group)
        == std::tie(rhs.depressed, rhs.locked, rhs.latched, rhs.group);
}

struct KeyState {
    uint32_t code = 0; // evdev code delivered to the client
};

// Alt on PC keyboards, or Command (reported as Meta) with Option (Alt) beside it.
enum class KeyboardModifierLayout {
    Pc,
    Apple,
};

// Command makes the key next to the space bar the primary shortcut modifier on every keyboard.
enum class ShortcutConvention {
    Pc,
    Command,
};

// Key repeat defaults. Wayland sends a rate in Hz and a delay in milliseconds.
constexpr int kDefaultKeyRepeatDelayMs = 500;
constexpr int kDefaultKeyRepeatIntervalMs = 40;

struct KeyRepeatSettings {
    bool enabled = true;
    std::chrono::milliseconds delay {kDefaultKeyRepeatDelayMs};
    std::chrono::milliseconds interval {kDefaultKeyRepeatIntervalMs};
};

inline bool operator==(const KeyRepeatSettings& lhs, const KeyRepeatSettings& rhs)
{
    return std::tie(lhs.enabled, lhs.delay, lhs.interval)
        == std::tie(rhs.enabled, rhs.delay, rhs.interval);
}

inline bool fuzzyEqual(const base::PointF &a, const base::PointF &b)
{
    const auto equal = [](double x, double y) {
        return x == 0 || y == 0 ? base::fuzzyIsNull(x - y) : base::fuzzyCompare(x, y);
    };
    return equal(a.x(), b.x()) && equal(a.y(), b.y());
}

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_TYPES_H_
