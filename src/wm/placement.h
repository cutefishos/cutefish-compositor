// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/window_positioning_utils.{h,cc}
//           chromium/src/ui/views/widget/native_widget_aura.cc (CenterWindow)
//           chromium/src/ui/gfx/geometry/rect.cc (AdjustToFit)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WM_PLACEMENT_H_
#define CUTEFISH_WM_PLACEMENT_H_

#include "base/geometry.h"

namespace cutefish::wm {

inline constexpr int kMinimumOnScreenArea = 30;

// The origin is kept.
void adjustBoundsSmallerThan(const base::Size &maxSize, base::Rect *bounds);

// An empty `grabArea` means `visibleArea`.
void adjustBoundsToEnsureMinimumVisibility(const base::Rect &visibleArea, base::Rect *bounds,
                                           const base::Rect &grabArea = base::Rect());

void adjustBoundsToFit(const base::Rect &area, base::Rect *bounds);

class Placement
{
public:
    base::Rect place(const base::Size &requestedSize, const base::Rect &workArea);

    base::Rect placeTransient(const base::Size &requestedSize, const base::Rect &parentBounds, int parentTopInset,
                         const base::Rect &workArea);
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_PLACEMENT_H_
