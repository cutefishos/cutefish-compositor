// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_INPUT_GESTURE_FILTER_H_
#define CUTEFISH_INPUT_GESTURE_FILTER_H_

#include <cstdint>

#include "input/pointer_gesture_delegate.h"

namespace cutefish::input {

// A gesture claimed at its begin is the compositor's to the end: no client sees any of it.
class GestureFilter
{
public:
    virtual ~GestureFilter() = default;

    virtual bool claimGesture(Timestamp timestamp, GestureType type, uint32_t fingers) = 0;
    virtual void onGestureUpdate(Timestamp timestamp, const GestureUpdate &update) = 0;
    virtual void onGestureEnd(Timestamp timestamp, bool cancelled) = 0;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_GESTURE_FILTER_H_
