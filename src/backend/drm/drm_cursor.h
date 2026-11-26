// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/hardware_display_controller.cc
//           (SetCursor, MoveCursor, NextCursorBuffer, DrawCursor)
//           chromium/src/ui/ozone/platform/drm/gpu/drm_dumb_buffer.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: one cursor buffer size from the device maximum instead
// of the Intel cursor-size table.

#ifndef CUTEFISH_BACKEND_DRM_DRM_CURSOR_H_
#define CUTEFISH_BACKEND_DRM_DRM_CURSOR_H_

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "backend/drm/drm_device_api.h"
#include "backend/drm/scanout_controller.h"
#include "base/geometry.h"
#include "render/color_transform.h"

namespace cutefish::backend {

class DrmCursor
{
public:
    DrmCursor(const DrmCursor&) = delete;
    DrmCursor& operator=(const DrmCursor&) = delete;
    ~DrmCursor();

    // Null when the device has no cursor plane or its buffers cannot be
    // allocated. When `cursorPlaneId` is non-zero on a driver whose legacy
    // cursor ioctls wait for flips, the cursor commits atomically through that
    // plane instead.
    static std::unique_ptr<DrmCursor> create(DrmDeviceApi* device, uint32_t crtcId,
                                             uint32_t cursorPlaneId = 0);

    bool usesAtomicPlane() const { return m_atomicPlaneId != 0; }
    uint32_t atomicPlaneId() const { return m_atomicPlaneId; }
    bool hasPendingAtomicUpdate() const;
    void setAtomicUpdateCallback(std::function<void()> callback)
    {
        m_onAtomicUpdate = std::move(callback);
    }
    PlaneContent atomicPlaneContent() const;
    void markAtomicCommitted(bool shown = true);
    void fallbackToLegacy();

    // `pixels` is premultiplied ARGB32; false when the image is larger than the
    // plane can show.
    bool setImage(const uint8_t* pixels, const base::Size& size, int stride,
                  const base::Point& hotspot);

    void setTint(const render::ColorTransform& tint);

    void hide();

    // `position` is where the hotspot should land, in output pixels.
    void move(const base::Point& position);

    // Keep the newest deferred image and position until the flip lands.
    void setFlipPending(bool pending);

    bool isVisible() const { return m_visible; }

private:
    struct Buffer {
        DumbBufferInfo info;
        void* data = nullptr;
        uint32_t framebufferId = 0;
    };

    DrmCursor(DrmDeviceApi* device, uint32_t crtcId, uint32_t atomicPlaneId,
              const base::Size& size, std::array<Buffer, 2> buffers);
    bool holdsForFlip() const;
    bool upload(bool notify = true);

    DrmDeviceApi* m_device = nullptr;
    uint32_t m_crtcId = 0;
    uint32_t m_atomicPlaneId = 0;
    base::Size m_size;
    std::array<Buffer, 2> m_buffers;
    size_t m_front = 0;
    // KMS may still read this buffer after hide(), until the plane changes.
    std::optional<size_t> m_atomicFront;
    std::string m_image;
    base::Size m_imageSize;
    int m_imageStride = 0;
    render::ColorTransform m_tint;
    base::Point m_imageHotspot;
    base::Point m_hotspot;
    base::Point m_position;
    // Until the first move the plane stays where the previous DRM master left it.
    bool m_positioned = false;
    bool m_visible = false;
    bool m_flipPending = false;
    bool m_uploadDeferred = false;
    bool m_moveDeferred = false;
    bool m_atomicDirty = false;
    bool m_failed = false;
    std::function<void()> m_onAtomicUpdate;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_DRM_CURSOR_H_
