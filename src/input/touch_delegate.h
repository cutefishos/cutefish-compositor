// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/touch_delegate.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_TOUCH_DELEGATE_H_
#define CUTEFISH_INPUT_TOUCH_DELEGATE_H_

#include <cstdint>


#include "input/types.h"
#include "base/geometry.h"

namespace cutefish::compositor {
class Surface;
}

namespace cutefish::input {

class Touch;

class TouchDelegate
{
public:
    // Called at the top of the touch device's destructor, to give the
    // delegate a chance to remove itself.
    virtual void onTouchDestroying(Touch* touch) = 0;

    virtual bool canAcceptTouchEventsForSurface(compositor::Surface* surface) const = 0;

    // `location` is relative to `surface`'s origin; every later event of this point carries the same `id`.
    virtual void onTouchDown(compositor::Surface* surface, Timestamp timestamp, int32_t id,
                             const base::PointF& location) = 0;

    virtual void onTouchUp(Timestamp timestamp, int32_t id) = 0;

    // `location` is relative to the surface the point went down on.
    virtual void onTouchMotion(Timestamp timestamp, int32_t id, const base::PointF& location) = 0;

    virtual void onTouchFrame() = 0;

    virtual void onTouchCancel() = 0;

protected:
    virtual ~TouchDelegate() = default;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_TOUCH_DELEGATE_H_
