// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/hardware_display_plane.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_BACKEND_DRM_DRM_PLANE_H_
#define CUTEFISH_BACKEND_DRM_DRM_PLANE_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "backend/drm/drm_properties.h"

namespace cutefish::backend {

struct DrmPlane {
    uint32_t id = 0;
    uint32_t type = 0;
    // Bit N is set when the CRTC at index N in drmModeRes may use this plane.
    uint32_t possibleCrtcs = 0;
    DrmPropertyMap properties;
    // An empty modifier list means only a legacy format list, where the
    // modifier is implicit.
    std::map<uint32_t, std::vector<uint64_t>> formats;

    bool canUseForCrtc(int crtcIndex) const
    {
        return crtcIndex >= 0 && crtcIndex < 32
            && (possibleCrtcs & (1u << static_cast<uint32_t>(crtcIndex)));
    }

    bool supportsFormat(uint32_t format, uint64_t modifier) const;

    static std::vector<std::string> requiredProperties();
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_DRM_PLANE_H_
