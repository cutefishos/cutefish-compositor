// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/
//               hardware_display_plane_manager.cc
//               (HardwareDisplayPlaneManager::IsCompatible / AssignOverlayPlanes)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include "backend/drm/plane_assignment.h"

#include <xf86drmMode.h>

#include <algorithm>

namespace cutefish::backend {

namespace {

bool isCompatible(const DrmPlane& plane, const PlaneRequest& request, int crtcIndex,
                  const std::vector<uint32_t>& used)
{
    if (plane.type == DRM_PLANE_TYPE_CURSOR)
        return false;
    if (!plane.canUseForCrtc(crtcIndex))
        return false;
    if (std::find(used.begin(), used.end(), plane.id) != used.end())
        return false;
    if (request.isPrimary != (plane.type == DRM_PLANE_TYPE_PRIMARY))
        return false;
    return plane.supportsFormat(request.format, request.modifier);
}

} // namespace

std::vector<PlaneAssignment> assignPlanes(const std::vector<DrmPlane>& planes, int crtcIndex,
                                          const std::vector<PlaneRequest>& requests)
{
    std::vector<PlaneAssignment> assignments;
    std::vector<uint32_t> used;
    assignments.reserve(requests.size());

    // The primary is picked first: it is not always the lowest-numbered plane,
    // and the ordered overlay scan would strand the overlays.
    for (size_t i = 0; i < requests.size(); ++i) {
        if (!requests[i].isPrimary)
            continue;
        const auto plane = std::find_if(planes.begin(), planes.end(), [&](const DrmPlane& plane) {
            return isCompatible(plane, requests[i], crtcIndex, used);
        });
        if (plane == planes.end())
            return {};
        assignments.push_back(PlaneAssignment {i, plane->id});
        used.push_back(plane->id);
    }

    // A single forward pass over planes in ascending id keeps the requested z
    // order.
    size_t next = 0;
    for (size_t i = 0; i < requests.size(); ++i) {
        if (requests[i].isPrimary)
            continue;
        bool placed = false;
        for (; next < planes.size(); ++next) {
            if (!isCompatible(planes[next], requests[i], crtcIndex, used))
                continue;
            assignments.push_back(PlaneAssignment {i, planes[next].id});
            used.push_back(planes[next].id);
            ++next;
            placed = true;
            break;
        }
        if (!placed)
            return {};
    }

    std::sort(assignments.begin(), assignments.end(),
              [](const PlaneAssignment& a, const PlaneAssignment& b) {
                  return a.requestIndex < b.requestIndex;
              });
    return assignments;
}

} // namespace cutefish::backend
