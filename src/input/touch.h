// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/touch.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_TOUCH_H_
#define CUTEFISH_INPUT_TOUCH_H_

#include <map>


#include "compositor/surface_observer.h"
#include "base/geometry.h"
#include "input/types.h"

namespace cutefish::input {

class Seat;
class TouchDelegate;

class Touch : public compositor::SurfaceObserver
{
public:
    // The delegate outlives the touch device, or destroys itself from onTouchDestroying().
    Touch(TouchDelegate* delegate, Seat* seat);
    Touch(const Touch&) = delete;
    Touch& operator=(const Touch&) = delete;
    ~Touch() override;

    TouchDelegate* delegate() const { return m_delegate; }

    // `target` is null when the point landed on nothing a client owns; `rootLocation` is in output coordinates.
    void onTouchDown(compositor::Surface* target, Timestamp timestamp, int32_t id,
                     const base::PointF& rootLocation);
    void onTouchMotion(Timestamp timestamp, int32_t id, const base::PointF& rootLocation);
    void onTouchUp(Timestamp timestamp, int32_t id);
    void onTouchCancel();
    void onTouchFrame();

    bool tracks(int32_t id) const { return m_points.contains(id); }

    void onSurfaceDestroying(compositor::Surface* surface) override;

private:
    void cancelAllTouches();

    TouchDelegate* m_delegate = nullptr;
    Seat* m_seat = nullptr;
    // Each point's surface, and how many points each surface holds, so a
    // surface is observed exactly while it has a point on it.
    std::map<int32_t, compositor::Surface*> m_points;
    std::map<compositor::Surface*, int> m_surfaceCounts;
    bool m_framePending = false;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_TOUCH_H_
