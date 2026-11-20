// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/keyboard.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_KEYBOARD_H_
#define CUTEFISH_INPUT_KEYBOARD_H_

#include <memory>
#include <set>

#include "compositor/surface_observer.h"
#include "input/keyboard_delegate.h"
#include "input/seat_observer.h"
#include "input/types.h"

namespace cutefish::input {

class Seat;

class Keyboard : public compositor::SurfaceObserver, public SeatObserver
{
public:
    Keyboard(std::unique_ptr<KeyboardDelegate> delegate, Seat* seat);
    Keyboard(const Keyboard&) = delete;
    Keyboard& operator=(const Keyboard&) = delete;
    ~Keyboard() override;

    KeyboardDelegate* delegate() const { return m_delegate.get(); }

    compositor::Surface* focusedSurface() const { return m_focus; }

    void onKey(Timestamp timestamp, uint32_t evdevCode, bool pressed);

    void onSurfaceDestroying(compositor::Surface* surface) override;

    void onSurfaceFocused(compositor::Surface* gainedFocus,
                          compositor::Surface* lostFocus) override;
    void onKeyboardModifierUpdated() override;
    void onKeyboardLayoutUpdated() override;
    void onKeyRepeatSettingsChanged() override;

private:
    void setFocus(compositor::Surface* surface);

    std::unique_ptr<KeyboardDelegate> m_delegate;
    Seat* m_seat = nullptr;

    compositor::Surface* m_focus = nullptr;
    // Another client borrows the keys; `m_focus` keeps focus without a leave.
    bool m_lent = false;
    // Keys held when the borrow ended: neither their press nor release reaches this client.
    std::set<uint32_t> m_borrowedKeys;

    // Apart from the seat's set, so a key pressed before focus is still released to this surface.
    KeyboardDelegate::PressedKeys m_pressedKeys;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_KEYBOARD_H_
