// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/common/drm_wrapper.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_BACKEND_DRM_DRM_DEVICE_API_H_
#define CUTEFISH_BACKEND_DRM_DRM_DEVICE_API_H_

#include "base/geometry.h"

#include <cstdint>
#include <vector>

#include "render/dmabuf.h"

struct gbm_device;

namespace cutefish::backend {

struct AtomicProperty {
    uint32_t objectId = 0;
    uint32_t propertyId = 0;
    uint64_t value = 0;
};

struct AtomicRequest {
    std::vector<AtomicProperty> properties;

    void add(uint32_t objectId, uint32_t propertyId, uint64_t value)
    {
        // A property the driver does not expose has id 0 and is skipped, which
        // is how optional properties like IN_FENCE_FD stay optional.
        if (propertyId)
            properties.push_back(AtomicProperty {objectId, propertyId, value});
    }
};

struct FramebufferParams {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    uint64_t modifier = 0;
    size_t planeCount = 0;
    uint32_t handles[4] = {};
    uint32_t strides[4] = {};
    uint32_t offsets[4] = {};
};

struct DumbBufferInfo {
    uint32_t handle = 0;
    uint32_t stride = 0;
    uint64_t size = 0;
};

class DrmDeviceApi
{
public:
    virtual ~DrmDeviceApi() = default;

    virtual int fd() const = 0;

    // The renderer's GBM device when it shares this file, else null. The file's
    // GEM handles are Mesa's then, so client buffers are imported through GBM.
    virtual gbm_device* gbmDevice() const = 0;

    virtual bool addFramebuffer(const FramebufferParams& params, uint32_t* framebufferId) = 0;
    // A plane still showing the framebuffer keeps showing it until a commit
    // replaces it (CLOSEFB), so a session's last frame outlives the session.
    virtual bool removeFramebuffer(uint32_t framebufferId) = 0;

    // The framebuffer `crtcId` scans out now, as DMA-BUF planes; false when it
    // shows nothing or this file is not DRM master.
    virtual bool exportCrtcFramebuffer(uint32_t crtcId, render::DmaBufAttributes* attributes) = 0;

    // The handle must be closed once no framebuffer references it.
    virtual bool primeFdToHandle(int fd, uint32_t* handle) = 0;
    virtual bool closeHandle(uint32_t handle) = 0;

    virtual bool createPropertyBlob(const void* data, size_t size, uint32_t* id) = 0;
    virtual void destroyPropertyBlob(uint32_t id) = 0;

    // `flags` are DRM_MODE_ATOMIC_*; `userData` is the page-flip event token
    // and must be null for a TEST_ONLY commit.
    virtual bool atomicCommit(const AtomicRequest& request, uint32_t flags, void* userData) = 0;

    virtual bool setCursor(uint32_t crtcId, uint32_t handle, const base::Size& size,
                           const base::Point& hotspot) = 0;
    virtual bool moveCursor(uint32_t crtcId, const base::Point& position) = 0;

    // Whether the driver's cursor ioctls block until the CRTC's pending page
    // flip lands.
    virtual bool cursorWaitsForFlips() const = 0;

    // Whether flips reported without a vblank count are still stamped when the
    // display took them.
    virtual bool flipTimesMarkRefresh() const = 0;

    // Empty when there is no cursor plane.
    virtual base::Size maxCursorSize() const = 0;

    virtual bool createDumbBuffer(const base::Size& size, DumbBufferInfo* info) = 0;
    virtual bool destroyDumbBuffer(uint32_t handle) = 0;
    // Returns nullptr on failure. `size` is DumbBufferInfo::size.
    virtual void* mapDumbBuffer(uint32_t handle, uint64_t size) = 0;
    virtual void unmapDumbBuffer(void* data, uint64_t size) = 0;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_DRM_DEVICE_API_H_
