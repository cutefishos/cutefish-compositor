// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/shell_surface_util.cc
//           (GetTargetSurfaceForLocatedEvent)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_HIT_TEST_H_
#define CUTEFISH_INPUT_HIT_TEST_H_

#include "base/geometry.h"

namespace cutefish::compositor {
class Compositor;
class Surface;
} // namespace cutefish::compositor

namespace cutefish::scene {
class Node;
}

namespace cutefish::input {

class SessionGate;

// `location` is `root`-local; `surfaceLocation` receives it in the returned surface's coordinates.
compositor::Surface *findTargetSurface(compositor::Surface *root, const base::PointF &location,
                                       base::PointF *surfaceLocation);

// `location` is in output coordinates.
compositor::Surface *findTargetSurface(compositor::Compositor *compositor, const base::PointF &location,
                                       base::PointF *surfaceLocation);

// Null `container` finds nothing.
compositor::Surface *findTargetSurfaceIn(compositor::Compositor *compositor, scene::Node *container,
                                         const base::PointF &location, base::PointF *surfaceLocation);

// Skips the surfaces that captures leave out.
compositor::Surface *findCapturedSurfaceIn(compositor::Compositor *compositor,
                                           scene::Node *container, const base::PointF &location);

bool isInputMethodSurface(const compositor::Compositor *compositor,
                          const compositor::Surface *surface);

// Null when `gate` leaves nothing targetable.
scene::Node *inputRootFor(compositor::Compositor *compositor, const SessionGate *gate);

// `location` is in output coordinates. Ignores input regions, so a grab can report points outside the surface.
base::PointF locationInSurface(const compositor::Surface *surface, const base::PointF &location);

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_HIT_TEST_H_
