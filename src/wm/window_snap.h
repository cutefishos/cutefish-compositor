// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modelled on chromium/src/ash/wm/workspace/workspace_window_resizer.cc
// (revision 7acd40ddefff49c9d36144999aafdbb674dbd07a).

#ifndef CUTEFISH_WM_WINDOW_SNAP_H_
#define CUTEFISH_WM_WINDOW_SNAP_H_

#include "base/geometry.h"
#include "wm/types.h"

namespace cutefish::wm {

enum class SnapType {
    None,
    Primary, // Left half.
    Secondary, // Right half.
    Maximize,
};

// The tolerance is only added on edges where the work area reaches the output.
base::Rect nonSnapArea(const base::Rect &workArea, const base::Rect &outputBounds);

// `location` is in output coordinates.
SnapType snapTypeAt(const base::PointF &location, const base::Rect &workArea, const base::Rect &outputBounds);

// Empty for SnapType::None.
base::Rect snapBounds(SnapType type, const base::Rect &workArea);

WindowStateType snapStateType(SnapType type);

bool canSnapSize(const base::Size &minimumOuterSize, const base::Rect &workArea);

// Full work-area height, against one of its vertical edges.
bool areBoundsValidSnappedBounds(WindowStateType type, const base::Rect &bounds, const base::Rect &workArea);

const char *toString(SnapType type);

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_SNAP_H_
