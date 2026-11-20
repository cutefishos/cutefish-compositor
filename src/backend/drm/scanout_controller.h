// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/
//               hardware_display_plane_manager_atomic.cc (Commit
//                   AddOutFencePtrProperties AddAllPendingCrtcProperties)
//               hardware_display_plane_manager.cc (UpdatePendingCrtcState)
//               hardware_display_plane_atomic.cc (AssignPlaneProps, SetPlaneProps)
//               drm_overlay_validator.cc (TestPageFlip)
//               drm_gpu_util.cc (CreateCTMBlob CreateLutBlob)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_BACKEND_DRM_SCANOUT_CONTROLLER_H_
#define CUTEFISH_BACKEND_DRM_SCANOUT_CONTROLLER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "backend/drm/drm_device_api.h"
#include "backend/drm/drm_plane.h"
#include "backend/drm/drm_properties.h"
#include "base/geometry.h"
#include "render/color_transform.h"
#include "render/scanout.h"
#include "render/sync_fence.h"
#include "scene/overlay_candidate.h"

namespace cutefish::backend {

struct PlaneContent {
    uint32_t planeId = 0;
    uint32_t framebufferId = 0;
    uint32_t format = 0;
    uint64_t modifier = 0;
    base::Rect displayRect;
    base::Rect sourceRect;
    // Owned: a frame can be committed again after its plan is gone, and KMS
    // dups the fd on every commit.
    std::shared_ptr<render::SyncFence> acquireFence;
    // Held while KMS may read the buffer; null for the compositor's own
    // framebuffer.
    std::shared_ptr<render::ScanoutBuffer> buffer;
    scene::SurfaceNode *node = nullptr;

    bool isDirectScanout() const { return buffer != nullptr; }

    int acquireFenceFd() const { return acquireFence ? acquireFence->get() : -1; }
};

class ScanoutController
{
public:
    struct Config {
        uint32_t crtcId = 0;
        // Index into drmModeRes, which possible_crtcs bits refer to.
        int crtcIndex = -1;
        uint32_t connectorId = 0;
        DrmPropertyMap crtcProperties;
        DrmPropertyMap connectorProperties;
        std::vector<DrmPlane> planes;
        base::Size modeSize;
    };

    ScanoutController(DrmDeviceApi *device, Config config);
    ScanoutController(const ScanoutController &) = delete;
    ScanoutController &operator=(const ScanoutController &) = delete;
    ~ScanoutController();

    // Zero is a normal configuration, not a failure.
    size_t maxOverlays() const { return m_overlayPlanes.size(); }

    bool hasPrimaryPlane() const { return m_primaryPlane != nullptr; }

    // Drops the topmost overlay until TEST_ONLY passes; returns the planes
    // primary first, or just `composited`.
    std::vector<PlaneContent> validate(const scene::OverlayPlan &plan,
                                       const PlaneContent &composited, uint32_t modeBlobId);

    // A modeset commits blocking without an event, since drivers reject
    // ALLOW_MODESET with a non-blocking flip; `releaseFence` may be null;
    // `secure` turns off every plane not in `planes`.
    bool commit(const std::vector<PlaneContent> &planes, bool modeset, void *userData,
                uint32_t modeBlobId, render::SyncFence *releaseFence, bool secure = false);

    bool disable();

    // Our hardware cursor uses the legacy cursor calls, so a modeset leaves
    // that plane alone.
    void setHardwareCursor(bool owned) { m_hardwareCursor = owned; }

    // Through its CTM, or a gamma LUT where it has no CTM; cursor plane
    // included.
    bool supportsColorTransform() const;

    // Returns whether the next commit changes; a CRTC that cannot take it stays
    // untinted.
    bool setColorTransform(const render::ColorTransform &transform);

    // Cached on the buffer; zero when this device cannot scan it out.
    uint32_t framebufferFor(render::ScanoutBuffer &buffer, uint32_t *format, uint64_t *modifier);

    uint64_t testFailures() const { return m_testFailures; }

    uint64_t importFailures() const { return m_importFailures; }

private:
    friend class DrmOutput;

    // Zero unsets a property, as the identity does.
    struct ColorPipeline {
        render::ColorTransform transform;
        uint32_t ctmBlob = 0;
        uint32_t gammaBlob = 0;
    };

    AtomicRequest buildRequest(const std::vector<PlaneContent> &planes, bool modeset,
                               uint32_t modeBlobId, bool off, bool secure = false) const;
    void destroyBlobs(const ColorPipeline &color);
    std::vector<const DrmPlane *> allPlanes() const;
    void addPlaneProperties(AtomicRequest &request, const DrmPlane &plane,
                            const PlaneContent &content, bool off) const;
    const DrmPlane *planeById(uint32_t id) const;

    DrmDeviceApi *m_device = nullptr;
    Config m_config;
    const DrmPlane *m_primaryPlane = nullptr;
    std::vector<const DrmPlane *> m_overlayPlanes;
    std::vector<const DrmPlane *> m_cursorPlanes;
    bool m_hardwareCursor = false;
    // Planes left out of the next commit are disabled explicitly.
    std::vector<uint32_t> m_activePlanes;
    ColorPipeline m_color;
    bool m_colorPending = false;
    bool m_colorRejected = false;
    uint64_t m_testFailures = 0;
    uint64_t m_importFailures = 0;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_SCANOUT_CONTROLLER_H_
