// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/
//               hardware_display_plane_manager.cc
//               (HardwareDisplayPlaneManager::IsCompatible / AssignOverlayPlanes)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_BACKEND_DRM_PLANE_ASSIGNMENT_H_
#define CUTEFISH_BACKEND_DRM_PLANE_ASSIGNMENT_H_

#include <cstdint>
#include <vector>

#include "backend/drm/drm_plane.h"

namespace cutefish::backend {

struct PlaneRequest {
    uint32_t format = 0;
    uint64_t modifier = 0;
    // z 0 must land on the primary plane; everything above it on an overlay.
    bool isPrimary = false;
};

struct PlaneAssignment {
    size_t requestIndex = 0;
    uint32_t planeId = 0;
};

// Empty when any request cannot be placed; cursor planes are never handed out
// here.
std::vector<PlaneAssignment> assignPlanes(const std::vector<DrmPlane>& planes, int crtcIndex,
                                          const std::vector<PlaneRequest>& requests);

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_PLANE_ASSIGNMENT_H_
