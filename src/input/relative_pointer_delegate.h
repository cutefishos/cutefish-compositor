// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/relative_pointer_delegate.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_RELATIVE_POINTER_DELEGATE_H_
#define CUTEFISH_INPUT_RELATIVE_POINTER_DELEGATE_H_


#include "input/types.h"
#include "base/geometry.h"

namespace cutefish::input {

class Pointer;

// One physical motion, unclipped by the display edges and by any pointer
// constraint. `delta` is accelerated and in logical units; `unaccelerated` is
// the device's own delta before acceleration.
struct RelativeMotion {
    base::PointF delta;
    base::PointF unaccelerated;
};

class RelativePointerDelegate
{
public:
    // Called at the top of the pointer's destructor, to give observers a
    // chance to remove themselves.
    virtual void onPointerDestroying(Pointer* pointer) = 0;

    // Motion while the pointer's client has pointer focus. Followed by the
    // pointer's own frame.
    virtual void onPointerRelativeMotion(Timestamp timestamp, const RelativeMotion& motion) = 0;

protected:
    virtual ~RelativePointerDelegate() = default;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_RELATIVE_POINTER_DELEGATE_H_
