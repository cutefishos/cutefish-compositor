// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/window_positioner.{h,cc}
//           (GetBoundsAndShowStateForNewWindow, GetReferenceWindow)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: a new window cascades from its application's latest one
// instead of taking its bounds, since the client chose its size before mapping.

#ifndef CUTEFISH_WM_WINDOW_POSITIONER_H_
#define CUTEFISH_WM_WINDOW_POSITIONER_H_

#include "base/geometry.h"

namespace cutefish::wm {

class Window;

namespace window_positioner {

base::Rect boundsForNewWindow(const Window *window, base::Rect bounds);

} // namespace window_positioner

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_POSITIONER_H_
