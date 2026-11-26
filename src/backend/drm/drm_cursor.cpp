// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/hardware_display_controller.cc
//           (SetCursor, MoveCursor, NextCursorBuffer, DrawCursor)
//           chromium/src/ui/ozone/platform/drm/gpu/drm_dumb_buffer.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include "backend/drm/drm_cursor.h"

#include <drm_fourcc.h>

#include <algorithm>
#include <cstring>

#include "core/logging.h"

namespace cutefish::backend {

DrmCursor::DrmCursor(DrmDeviceApi* device, uint32_t crtcId, uint32_t atomicPlaneId,
                     const base::Size& size, std::array<Buffer, 2> buffers)
    : m_device(device)
    , m_crtcId(crtcId)
    , m_atomicPlaneId(atomicPlaneId)
    , m_size(size)
    , m_buffers(buffers)
{
}

DrmCursor::~DrmCursor()
{
    if (!usesAtomicPlane())
        hide();
    for (Buffer& buffer : m_buffers) {
        if (buffer.framebufferId)
            m_device->removeFramebuffer(buffer.framebufferId);
        m_device->unmapDumbBuffer(buffer.data, buffer.info.size);
        if (buffer.info.handle)
            m_device->destroyDumbBuffer(buffer.info.handle);
    }
}

std::unique_ptr<DrmCursor> DrmCursor::create(DrmDeviceApi* device, uint32_t crtcId,
                                             uint32_t cursorPlaneId)
{
    if (!device || !crtcId)
        return nullptr;

    const base::Size size = device->maxCursorSize();
    if (size.isEmpty())
        return nullptr;

    std::array<Buffer, 2> buffers = {};
    for (Buffer& buffer : buffers) {
        if (!device->createDumbBuffer(size, &buffer.info))
            break;
        buffer.data = device->mapDumbBuffer(buffer.info.handle, buffer.info.size);
        if (!buffer.data)
            break;
        std::memset(buffer.data, 0, buffer.info.size);
    }

    const bool allocated = buffers[0].data && buffers[1].data;
    if (!allocated) {
        for (Buffer& buffer : buffers) {
            device->unmapDumbBuffer(buffer.data, buffer.info.size);
            if (buffer.info.handle)
                device->destroyDumbBuffer(buffer.info.handle);
        }
        CF_INFO(cfDrm) << "No hardware cursor buffers; the cursor stays composited";
        return nullptr;
    }

    uint32_t atomicPlaneId = 0;
    if (cursorPlaneId != 0 && device->cursorWaitsForFlips()) {
        bool fbOk = true;
        for (Buffer& buffer : buffers) {
            FramebufferParams params;
            params.width = static_cast<uint32_t>(size.width());
            params.height = static_cast<uint32_t>(size.height());
            params.format = DRM_FORMAT_ARGB8888;
            params.modifier = DRM_FORMAT_MOD_INVALID;
            params.planeCount = 1;
            params.handles[0] = buffer.info.handle;
            params.strides[0] = buffer.info.stride;
            params.offsets[0] = 0;
            if (!device->addFramebuffer(params, &buffer.framebufferId)) {
                fbOk = false;
                break;
            }
        }
        if (fbOk) {
            atomicPlaneId = cursorPlaneId;
        } else {
            for (Buffer& buffer : buffers) {
                if (buffer.framebufferId) {
                    device->removeFramebuffer(buffer.framebufferId);
                    buffer.framebufferId = 0;
                }
            }
        }
    }

    return std::unique_ptr<DrmCursor>(
            new DrmCursor(device, crtcId, atomicPlaneId, size, buffers));
}

bool DrmCursor::setImage(const uint8_t* pixels, const base::Size& size, int stride,
                         const base::Point& hotspot)
{
    if (m_failed || !pixels || size.isEmpty())
        return false;
    if (size.width() > m_size.width() || size.height() > m_size.height())
        return false;

    const size_t bytes = size_t(stride) * (size.height() - 1) + size_t(size.width()) * 4;
    if ((m_visible || m_uploadDeferred) && size == m_imageSize && stride == m_imageStride
        && hotspot == m_imageHotspot && m_image.size() == bytes
        && std::memcmp(m_image.data(), pixels, bytes) == 0) {
        return true;
    }

    m_image.assign(reinterpret_cast<const char*>(pixels), bytes);
    m_imageSize = size;
    m_imageStride = stride;
    m_imageHotspot = hotspot;
    if (holdsForFlip() && (usesAtomicPlane() || m_visible)) {
        m_uploadDeferred = true;
        return true;
    }
    return upload();
}

void DrmCursor::setTint(const render::ColorTransform& tint)
{
    if (tint == m_tint)
        return;
    m_tint = tint;
    if (!m_visible || m_image.empty())
        return;
    if (holdsForFlip())
        m_uploadDeferred = true;
    else
        upload();
}

bool DrmCursor::holdsForFlip() const
{
    return m_flipPending && m_device->cursorWaitsForFlips();
}

bool DrmCursor::hasPendingAtomicUpdate() const
{
    // Disabling an already detached plane does not produce a flip event.
    return m_atomicDirty && (m_atomicFront || (m_visible && m_positioned));
}

void DrmCursor::markAtomicCommitted(bool shown)
{
    m_atomicDirty = false;
    if (shown && m_visible && m_positioned)
        m_atomicFront = m_front;
    else
        m_atomicFront.reset();
}

PlaneContent DrmCursor::atomicPlaneContent() const
{
    PlaneContent content;
    content.planeId = m_atomicPlaneId;
    if (!m_visible || !m_positioned || m_buffers[m_front].framebufferId == 0)
        return content;
    content.framebufferId = m_buffers[m_front].framebufferId;
    content.format = DRM_FORMAT_ARGB8888;
    content.modifier = DRM_FORMAT_MOD_INVALID;
    content.sourceRect = base::Rect(base::Point(), m_size);
    content.displayRect = base::Rect(m_position - m_hotspot, m_size);
    content.hotspot = m_hotspot;
    return content;
}

void DrmCursor::fallbackToLegacy()
{
    if (!usesAtomicPlane())
        return;
    CF_INFO(cfDrm) << "Atomic cursor commit failed; falling back to legacy cursor ioctls";
    m_atomicPlaneId = 0;
    m_atomicDirty = false;
    m_onAtomicUpdate = nullptr;
    if (m_atomicFront) {
        m_front = *m_atomicFront;
        m_atomicFront.reset();
        if (!m_visible)
            m_device->setCursor(m_crtcId, 0, base::Size(-1, -1), base::Point());
    }
    if ((m_visible || m_uploadDeferred) && !m_image.empty()) {
        if (holdsForFlip())
            m_uploadDeferred = true;
        else
            upload(false);
    }
}

bool DrmCursor::upload(bool notify)
{
    m_uploadDeferred = false;
    // Never write into the buffer the display is scanning out.
    const size_t back = usesAtomicPlane()
            ? (m_atomicFront ? (*m_atomicFront ^ 1) : m_front)
            : (m_visible ? (m_front ^ 1) : m_front);
    Buffer& buffer = m_buffers[back];

    std::memset(buffer.data, 0, buffer.info.size);
    auto* destination = static_cast<uint8_t*>(buffer.data);
    const auto* pixels = reinterpret_cast<const uint8_t*>(m_image.data());
    const int rowBytes = m_imageSize.width() * 4;
    for (int y = 0; y < m_imageSize.height(); ++y) {
        uint8_t* row = destination + static_cast<size_t>(y) * buffer.info.stride;
        std::memcpy(row, pixels + static_cast<size_t>(y) * m_imageStride,
                    static_cast<size_t>(rowBytes));
        if (m_tint.isIdentity())
            continue;
        const auto scaled = [](uint8_t value, float gain) {
            return uint8_t(std::min(255.0f, value * gain + 0.5f));
        };
        // The plane reads ARGB8888: blue, green, red and alpha in memory.
        for (int x = 0; x < rowBytes; x += 4) {
            row[x] = scaled(row[x], m_tint.blue);
            row[x + 1] = scaled(row[x + 1], m_tint.green);
            row[x + 2] = scaled(row[x + 2], m_tint.red);
        }
    }

    m_hotspot = m_imageHotspot;
    if (usesAtomicPlane()) {
        m_front = back;
        m_visible = true;
        m_atomicDirty = true;
        if (notify && m_positioned && !m_flipPending && m_onAtomicUpdate)
            m_onAtomicUpdate();
        return true;
    }

    if (!m_device->setCursor(m_crtcId, buffer.info.handle, m_size, m_hotspot)) {
        CF_INFO(cfDrm) << "The driver rejected the hardware cursor; compositing it instead";
        m_failed = true;
        m_visible = false;
        return false;
    }

    m_front = back;
    m_visible = true;
    if (m_positioned) {
        m_moveDeferred = false;
        m_device->moveCursor(m_crtcId, m_position - m_hotspot);
    }
    return true;
}

void DrmCursor::hide()
{
    m_uploadDeferred = false;
    m_moveDeferred = false;
    if (!m_visible)
        return;
    m_visible = false;
    if (usesAtomicPlane()) {
        m_atomicDirty = true;
        if (m_atomicFront && !m_flipPending && m_onAtomicUpdate)
            m_onAtomicUpdate();
        return;
    }
    m_device->setCursor(m_crtcId, 0, base::Size(-1, -1), base::Point());
}

void DrmCursor::move(const base::Point& position)
{
    if (usesAtomicPlane() && m_positioned && position == m_position)
        return;
    m_position = position;
    m_positioned = true;
    if (!m_visible)
        return;
    if (usesAtomicPlane()) {
        m_atomicDirty = true;
        if (!m_flipPending && m_onAtomicUpdate)
            m_onAtomicUpdate();
        return;
    }
    if (holdsForFlip()) {
        m_moveDeferred = true;
        return;
    }
    m_device->moveCursor(m_crtcId, position - m_hotspot);
}

void DrmCursor::setFlipPending(bool pending)
{
    m_flipPending = pending;
    if (pending)
        return;
    if (usesAtomicPlane()) {
        if (m_uploadDeferred)
            upload(false);
        return;
    }
    if (m_uploadDeferred) {
        upload();
        return;
    }
    if (!m_moveDeferred)
        return;
    m_moveDeferred = false;
    if (m_visible)
        m_device->moveCursor(m_crtcId, m_position - m_hotspot);
}

} // namespace cutefish::backend
