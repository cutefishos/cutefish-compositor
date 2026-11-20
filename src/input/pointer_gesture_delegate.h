// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/pointer_gesture_pinch_delegate.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_POINTER_GESTURE_DELEGATE_H_
#define CUTEFISH_INPUT_POINTER_GESTURE_DELEGATE_H_

#include <cstdint>


#include "input/types.h"
#include "base/geometry.h"

namespace cutefish::compositor {
class Surface;
}

namespace cutefish::input {

class Pointer;

enum class GestureType {
    Swipe,
    Pinch,
    Hold,
};

struct GestureUpdate {
    // The fingers' motion, accelerated and in logical units.
    base::PointF delta;
    // The same motion without acceleration, as libinput reports it.
    base::PointF unacceleratedDelta;
    // Pinch only: the distance between the fingers relative to the begin,
    // and the rotation since the last update, in degrees clockwise.
    double scale = 1;
    double rotation = 0;
};

class PointerGestureDelegate
{
public:
    // Called at the top of the pointer's destructor, to give observers a
    // chance to remove themselves.
    virtual void onPointerDestroying(Pointer* pointer) = 0;

    virtual GestureType gestureType() const = 0;

    // `surface` has pointer focus; the gesture stays with it until the end.
    virtual void onPointerGestureBegin(Timestamp timestamp, compositor::Surface* surface,
                                       uint32_t fingers) = 0;
    // Not called for a hold.
    virtual void onPointerGestureUpdate(Timestamp timestamp, const GestureUpdate& update) = 0;
    virtual void onPointerGestureEnd(Timestamp timestamp, bool cancelled) = 0;

protected:
    virtual ~PointerGestureDelegate() = default;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_POINTER_GESTURE_DELEGATE_H_
