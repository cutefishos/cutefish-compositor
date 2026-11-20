// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/drm_overlay_plane.h
//           chromium/src/components/viz/service/display/
//               overlay_strategy_fullscreen.cc
//               overlay_strategy_single_on_top.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_SCENE_OVERLAY_CANDIDATE_H_
#define CUTEFISH_SCENE_OVERLAY_CANDIDATE_H_

#include <memory>
#include <vector>


#include "render/presentation.h"
#include "render/scanout.h"
#include "render/sync_fence.h"
#include "base/geometry.h"

namespace cutefish::scene {

class Node;
void removeOccludedPresentation(Node& root, const base::Size& logicalSize,
                                render::PresentationSubmission& submission);

class SurfaceNode;

struct OverlayCandidate {
    OverlayCandidate() = default;
    OverlayCandidate(OverlayCandidate&&) noexcept = default;
    OverlayCandidate& operator=(OverlayCandidate&&) noexcept = default;

    bool isValid() const { return buffer != nullptr; }

    // Holding it keeps the wl_buffer alive while the plane references it.
    std::shared_ptr<render::ScanoutBuffer> buffer;

    // Handed to KMS as the plane's IN_FENCE_FD rather than waited on.
    render::SyncFence acquireFence;

    // Where the content lands on the output, in output pixels.
    base::Rect displayRect;

    // In buffer pixels; must match displayRect's size, as planes are never scaled.
    base::Rect sourceRect;

    // No blending needed. Only opaque content may take the primary plane.
    bool opaque = false;

    // 0 is the primary plane; overlays are above it in ascending order.
    int zOrder = 0;

    // Never dereferenced after the frame it was proposed for.
    SurfaceNode* node = nullptr;
};

struct OverlayPlan {
    OverlayPlan() = default;
    OverlayPlan(OverlayPlan&&) noexcept = default;
    OverlayPlan& operator=(OverlayPlan&&) noexcept = default;

    // Invalid when the scene is composited as usual, which is never an error.
    OverlayCandidate primary;

    // Z ascending, all above the primary plane.
    std::vector<OverlayCandidate> overlays;

    bool hasDirectScanout() const { return primary.isValid(); }
    bool isEmpty() const { return !primary.isValid() && overlays.empty(); }
};

// Decides only what the scene permits; the display controller may still reject it.
OverlayPlan proposeOverlays(Node& root, const base::Size& outputSize, size_t maxOverlays,
                            double outputScale = 1);

} // namespace cutefish::scene

#endif // CUTEFISH_SCENE_OVERLAY_CANDIDATE_H_
