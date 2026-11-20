// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/surface_observer.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_COMPOSITOR_SURFACE_OBSERVER_H_
#define CUTEFISH_COMPOSITOR_SURFACE_OBSERVER_H_

#include <functional>

namespace cutefish::compositor {

class Surface;

class SurfaceObserver
{
public:
    virtual ~SurfaceObserver() = default;

    virtual void onSurfaceDestroying(Surface* surface) = 0;

    // Role owners hang their commit handling off this.
    virtual void onSurfaceCommit(Surface* surface) { }

    // May come before the buffer is ready; the returned function runs right
    // before this observer's onSurfaceCommit() for that commit.
    virtual std::function<void()> onSurfaceCommitRequested(Surface* surface) { return {}; }

    virtual void onScaleFactorChanged(Surface* surface) { }

    virtual void onSurfaceMenuChanged(Surface* surface) { }

    virtual void onSurfaceWindowHintsChanged(Surface* surface) { }

    // Validate cached state immediately before hierarchy application.
    virtual bool onSurfaceStateApplying(Surface* surface) { return true; }
};

} // namespace cutefish::compositor

#endif // CUTEFISH_COMPOSITOR_SURFACE_OBSERVER_H_
