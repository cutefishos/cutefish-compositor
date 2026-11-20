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

#include <string>

#include "base/geometry.h"

#include <array>
#include <cstdint>
#include <memory>

#include "backend/drm/drm_device_api.h"
#include "render/color_transform.h"

namespace cutefish::backend {

class DrmCursor
{
public:
    DrmCursor(const DrmCursor&) = delete;
    DrmCursor& operator=(const DrmCursor&) = delete;
    ~DrmCursor();

    // Null when the device has no cursor plane or its buffers cannot be
    // allocated.
    static std::unique_ptr<DrmCursor> create(DrmDeviceApi* device, uint32_t crtcId);

    // `pixels` is premultiplied ARGB32; false when the image is larger than the
    // plane can show.
    bool setImage(const uint8_t* pixels, const base::Size& size, int stride,
                  const base::Point& hotspot);

    void setTint(const render::ColorTransform& tint);

    void hide();

    // `position` is where the hotspot should land, in output pixels.
    void move(const base::Point& position);

    // On a device whose cursor ioctls wait for flips, a visible cursor keeps
    // only the newest image and position until the flip lands.
    void setFlipPending(bool pending);

    bool isVisible() const { return m_visible; }

private:
    struct Buffer {
        DumbBufferInfo info;
        void* data = nullptr;
    };

    DrmCursor(DrmDeviceApi* device, uint32_t crtcId, const base::Size& size,
              std::array<Buffer, 2> buffers);
    bool holdsForFlip() const;
    bool upload();

    DrmDeviceApi* m_device = nullptr;
    uint32_t m_crtcId = 0;
    base::Size m_size;
    std::array<Buffer, 2> m_buffers;
    size_t m_front = 0;
    std::string m_image;
    base::Size m_imageSize;
    int m_imageStride = 0;
    render::ColorTransform m_tint;
    base::Point m_hotspot;
    base::Point m_position;
    // Until the first move the plane stays where the previous DRM master left it.
    bool m_positioned = false;
    bool m_visible = false;
    bool m_flipPending = false;
    bool m_uploadDeferred = false;
    bool m_moveDeferred = false;
    bool m_failed = false;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_DRM_CURSOR_H_
