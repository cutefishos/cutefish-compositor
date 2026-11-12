// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/window_positioner.{h,cc}
//           (GetBoundsAndShowStateForNewWindow, GetReferenceWindow)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: the reference window is the application's own most
// recent window, and a new window cascades from it rather than taking its
// bounds, since a Wayland client has chosen its size before it maps. Ash's
// automatic side-by-side rearrangement is not ported, nor the maximized state
// it gives a new browser window: a client's window opens in the state it asks
// for.

#ifndef CUTEFISH_WM_WINDOW_POSITIONER_H_
#define CUTEFISH_WM_WINDOW_POSITIONER_H_

#include "base/geometry.h"

namespace cutefish::wm {

class Window;

namespace window_positioner {

// Where a new `window` at `bounds`, centered in its work area, should go:
// offset down and right from its application's most recent window on the same
// output, or from that window's restore bounds while it is maximized. A cascade
// that would run past the work area restarts from its top-left corner.
base::Rect boundsForNewWindow(const Window *window, base::Rect bounds);

} // namespace window_positioner

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_POSITIONER_H_
