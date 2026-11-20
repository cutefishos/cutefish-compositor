// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/buffer.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_COMPOSITOR_DMABUF_BUFFER_H_
#define CUTEFISH_COMPOSITOR_DMABUF_BUFFER_H_

#include <memory>
#include <utility>

#include "compositor/buffer.h"
#include "render/dmabuf.h"

namespace cutefish::compositor {

// Samples the client's storage directly, so wl_buffer.release waits until the
// buffer is off every surface.
class DmaBufBuffer : public Buffer, public render::ScanoutBuffer
{
public:
    // Takes ownership of the plane fds in `attributes` in every case; null when
    // `device` cannot import the buffer.
    static std::unique_ptr<DmaBufBuffer> create(render::DmaBufDevice* device,
                                                render::DmaBufAttributes attributes);

    ~DmaBufBuffer() override;

    base::Size size() const override { return m_attributes.size; }

    render::PixelFormat format() const override { return m_format; }

    const uint8_t* pixels() const override { return nullptr; }

    int stride() const override;

    std::shared_ptr<render::Texture> texture() const override { return m_texture; }

    // Still owning its plane fds, which the scanout path builds a DRM
    // framebuffer from.
    const render::DmaBufAttributes& attributes() const { return m_attributes; }

    const render::DmaBufAttributes& scanoutAttributes() const override { return m_attributes; }

    render::ScanoutBuffer* scanoutBuffer() override { return this; }

    bool supportsExplicitSync() const override { return true; }

    std::vector<render::SyncFence> implicitFences() const override;

    void onScanoutAcquired() override { Buffer::onScanoutAcquired(); }

    void onScanoutReleased(render::SyncFence fence) override
    {
        Buffer::onScanoutReleased(std::move(fence));
    }

private:
    DmaBufBuffer(render::DmaBufAttributes attributes, std::shared_ptr<render::Texture> texture,
                 render::PixelFormat format);

    render::DmaBufAttributes m_attributes;
    std::shared_ptr<render::Texture> m_texture;
    render::PixelFormat m_format = render::PixelFormat::Rgba8888;
};

} // namespace cutefish::compositor

#endif // CUTEFISH_COMPOSITOR_DMABUF_BUFFER_H_
