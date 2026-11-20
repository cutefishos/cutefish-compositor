// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/base/accelerators/accelerator.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>

#include <string>
#include <string_view>

namespace cutefish::accelerators {

enum Modifier : uint32_t {
    NoModifier = 0,
    ShiftDown = 1u << 0,
    ControlDown = 1u << 1,
    AltDown = 1u << 2,
    MetaDown = 1u << 3,
    AltGrDown = 1u << 4,
};

constexpr uint32_t kModifierMask = ShiftDown | ControlDown | AltDown | MetaDown | AltGrDown;

class Accelerator
{
public:
    enum class KeyState {
        Pressed,
        Released,
    };

    constexpr Accelerator() = default;

    constexpr Accelerator(uint32_t keysym, uint32_t modifiers,
                          KeyState keyState = KeyState::Pressed)
        : m_keysym(keysym)
        , m_modifiers(modifiers & kModifierMask)
        , m_keyState(keyState)
    {
    }

    // The evdev code pins the key position for the positional lookup.
    constexpr Accelerator(uint32_t keysym, uint32_t evdevCode, uint32_t modifiers,
                          KeyState keyState)
        : m_keysym(keysym)
        , m_evdevCode(evdevCode)
        , m_modifiers(modifiers & kModifierMask)
        , m_keyState(keyState)
    {
    }

    constexpr uint32_t keysym() const { return m_keysym; }

    constexpr uint32_t evdevCode() const { return m_evdevCode; }

    constexpr void resetEvdevCode() { m_evdevCode = 0; }

    constexpr uint32_t modifiers() const { return m_modifiers; }

    constexpr KeyState keyState() const { return m_keyState; }

    constexpr void setKeyState(KeyState state) { m_keyState = state; }

    constexpr bool isShiftDown() const { return m_modifiers & ShiftDown; }

    constexpr bool isControlDown() const { return m_modifiers & ControlDown; }

    constexpr bool isAltDown() const { return m_modifiers & AltDown; }

    constexpr bool isMetaDown() const { return m_modifiers & MetaDown; }

    constexpr bool isAltGrDown() const { return m_modifiers & AltGrDown; }

    // The evdev code is not part of the identity: it only selects a keysym.
    constexpr bool operator==(const Accelerator &rhs) const
    {
        return m_keysym == rhs.m_keysym && m_modifiers == rhs.m_modifiers
                && m_keyState == rhs.m_keyState;
    }

    constexpr bool operator!=(const Accelerator &rhs) const { return !(*this == rhs); }

    constexpr bool operator<(const Accelerator &rhs) const
    {
        return std::tie(m_keysym, m_modifiers, m_keyState)
                < std::tie(rhs.m_keysym, rhs.m_modifiers, rhs.m_keyState);
    }

    size_t hash() const
    {
        return static_cast<size_t>(m_keyState) | (static_cast<size_t>(m_modifiers) << 1)
                | (static_cast<size_t>(m_keysym) << 8);
    }

    std::string shortcutText() const;

private:
    uint32_t m_keysym = 0;
    uint32_t m_evdevCode = 0;
    uint32_t m_modifiers = NoModifier;
    KeyState m_keyState = KeyState::Pressed;
};

// The keysym name xkbcommon knows the key by ("Up", "XF86AudioRaiseVolume").
std::string keysymName(uint32_t keysym);

// 0 when the name is unknown.
uint32_t keysymFromName(const std::string &name);

// Empty for none; letters come back upper-cased.
std::string keysymText(uint32_t keysym);

// 0 when the code is not a positional key, or when the layout puts a letter or a
// digit there.
uint32_t positionalUsKeysym(uint32_t evdevCode, uint32_t layoutKeysym);

bool isModifierKeysym(uint32_t keysym);
bool isFunctionKeysym(uint32_t keysym);

} // namespace cutefish::accelerators

namespace std {
template<>
struct hash<cutefish::accelerators::Accelerator> {
    size_t operator()(const cutefish::accelerators::Accelerator &accelerator) const
    {
        return accelerator.hash();
    }
};
} // namespace std

#endif // CUTEFISH_ACCELERATORS_ACCELERATOR_H_
