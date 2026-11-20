// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/buffer.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_COMPOSITOR_BUFFER_H_
#define CUTEFISH_COMPOSITOR_BUFFER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "base/geometry.h"

#include "render/scanout.h"
#include "render/sync_fence.h"
#include "render/texture.h"

namespace cutefish::compositor {

class ShmPool;

// Must be signalled exactly once on every path, or the client waits forever; an
// invalid fence means already finished.
class BufferRelease
{
public:
    virtual ~BufferRelease() = default;

    // An invalid fence means the read is already done.
    virtual void signal(render::SyncFence fence) = 0;

    // A client waits on the newest point it set, so an older point on the same
    // timeline is done once replaced.
    virtual const void* timeline() const { return nullptr; }
    virtual uint64_t timelinePoint() const { return 0; }
};

// wl_buffer.release waits until the contents are consumed and, when
// retainsClientStorage(), the buffer is off every surface.
class Buffer
{
public:
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    virtual ~Buffer();

    virtual base::Size size() const = 0;
    virtual render::PixelFormat format() const = 0;

    // Null for buffers the renderer samples directly; valid only until the
    // buffer is released.
    virtual const uint8_t* pixels() const = 0;
    virtual int stride() const = 0;

    // endPixelAccess() returns false when the client's storage vanished
    // mid-read and zeros were read.
    virtual void beginPixelAccess() {}
    virtual bool endPixelAccess() { return true; }

    // Null for buffers whose pixels() have to be copied.
    virtual std::shared_ptr<render::Texture> texture() const { return nullptr; }

    virtual bool retainsClientStorage() const { return true; }

    // Non-null for buffers a display controller may scan out directly.
    virtual render::ScanoutBuffer* scanoutBuffer() { return nullptr; }

    virtual bool supportsExplicitSync() const { return false; }

    // Empty when nothing is pending or the kernel cannot tell; a commit is held
    // until these signal.
    virtual std::vector<render::SyncFence> implicitFences() const { return {}; }

    void setReleaseCallback(std::function<void()> callback);

    // Invoked when the client's storage vanished while being read, a protocol
    // violation.
    void setFaultCallback(std::function<void()> callback);

    // Cached attachments cannot be released by another surface's upload.
    void onAttach(bool cached = false);
    void onDetach(bool cached = false);

    // `fence`, when valid, is when the GPU or the display controller finishes
    // the read.
    void onContentsConsumed(render::SyncFence fence = render::SyncFence());

    // Invalid means the buffer is ready now, which is also the implicit-sync
    // case.
    const render::SyncFence& acquireFence() const { return m_acquireFence; }
    void setAcquireFence(render::SyncFence fence) { m_acquireFence = std::move(fence); }

    // The same buffer may be committed to several surfaces, so these
    // accumulate.
    void addRelease(std::shared_ptr<BufferRelease> release);

    size_t pendingReleaseCount() const { return m_releases.size(); }

    // Release waits for this too, or a reattaching client would get its storage
    // back while KMS still shows it.
    void onScanoutAcquired();
    void onScanoutReleased(render::SyncFence fence);

    uint32_t attachCount() const { return m_attachCount; }

    bool contentsInUse() const { return m_contentsInUse; }

    bool isReleased() const { return m_released; }

protected:
    Buffer();

    void reportFault();

private:
    void maybeRelease();
    // Signals and drops every owed release. Safe to call more than once.
    void signalReleases(const render::SyncFence& fence);
    void addConsumedFence(render::SyncFence fence);

    std::function<void()> m_releaseCallback;
    std::function<void()> m_faultCallback;
    render::SyncFence m_acquireFence;
    std::vector<std::shared_ptr<BufferRelease>> m_releases;
    // Kept until release so a late detach still reports the right completion.
    render::SyncFence m_consumedFence;
    uint32_t m_scanoutCount = 0;
    uint32_t m_attachCount = 0;
    uint32_t m_cachedAttachCount = 0;
    bool m_contentsInUse = false;
    bool m_released = true;
};

class ShmBuffer : public Buffer
{
public:
    ShmBuffer(std::shared_ptr<ShmPool> pool, const base::Size& size, int32_t offset, int32_t stride,
              render::PixelFormat format);
    ~ShmBuffer() override;

    base::Size size() const override { return m_size; }

    render::PixelFormat format() const override { return m_format; }

    const uint8_t* pixels() const override;

    int stride() const override { return m_stride; }

    void beginPixelAccess() override;
    bool endPixelAccess() override;

    // The pixels are copied into a texture, so the client may reuse its storage
    // once the copy is issued.
    bool retainsClientStorage() const override { return false; }

    int32_t offset() const { return m_offset; }

    const ShmPool* pool() const { return m_pool.get(); }

private:
    std::shared_ptr<ShmPool> m_pool;
    base::Size m_size;
    int32_t m_offset = 0;
    int32_t m_stride = 0;
    render::PixelFormat m_format = render::PixelFormat::Bgrx8888;
};

} // namespace cutefish::compositor

#endif // CUTEFISH_COMPOSITOR_BUFFER_H_
