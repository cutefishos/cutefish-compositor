// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/drm_framebuffer.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_BACKEND_DRM_DRM_FRAMEBUFFER_H_
#define CUTEFISH_BACKEND_DRM_DRM_FRAMEBUFFER_H_

#include "base/geometry.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "render/scanout.h"

struct gbm_bo;

namespace cutefish::backend {

class DrmDeviceApi;

// For a GBM bo the framebuffer is the bo's user data and dies with it; for a
// client DMA-BUF it is cached on the ScanoutBuffer.
class DrmFramebuffer : public render::ScanoutBuffer::Import
{
public:
    DrmFramebuffer(const DrmFramebuffer &) = delete;
    DrmFramebuffer &operator=(const DrmFramebuffer &) = delete;
    ~DrmFramebuffer() override;

    // Ownership stays with the bo; null if the framebuffer cannot be created.
    static DrmFramebuffer *forBuffer(DrmDeviceApi *device, gbm_bo *bo);

    static DrmFramebuffer *importForBuffer(DrmDeviceApi *device, gbm_bo *bo);

    // A dumb framebuffer filled with a copy of `pixels`, for a device that
    // cannot import `bo`.
    static DrmFramebuffer *copyForBuffer(DrmDeviceApi *device, gbm_bo *bo,
                                         const uint8_t *pixels, uint32_t stride);

    // Null when the device cannot scan the buffer out, which falls back to
    // composition.
    static std::unique_ptr<DrmFramebuffer> fromDmaBuf(DrmDeviceApi *device,
                                                      const render::DmaBufAttributes &attributes);

    static std::unique_ptr<DrmFramebuffer> fromPixels(DrmDeviceApi *device, const base::Size &size,
                                                      const uint8_t *pixels, uint32_t stride);

    uint32_t id() const { return m_id; }

    base::Size size() const { return m_size; }

    uint32_t format() const { return m_format; }

    uint64_t modifier() const { return m_modifier; }

private:
    DrmFramebuffer(DrmDeviceApi *device, uint32_t id, const base::Size &size, uint32_t format,
                   uint64_t modifier, std::vector<uint32_t> ownedHandles);

    static void destroyCallback(gbm_bo *bo, void *data);
    static DrmFramebuffer *keep(gbm_bo *bo, std::unique_ptr<DrmFramebuffer> framebuffer);
    bool fill(const uint8_t *pixels, uint32_t stride);

    DrmDeviceApi *m_device = nullptr;
    uint32_t m_id = 0;
    base::Size m_size;
    uint32_t m_format = 0;
    uint64_t m_modifier = 0;
    // Empty for a GBM bo, whose handles belong to GBM.
    std::vector<uint32_t> m_ownedHandles;
    // Its handles are owned by the device's GBM device.
    gbm_bo *m_importedBo = nullptr;
    uint32_t m_dumbHandle = 0;
    uint32_t m_dumbStride = 0;
    uint64_t m_dumbSize = 0;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_DRM_FRAMEBUFFER_H_
