// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/shared_memory.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_COMPOSITOR_SHM_POOL_H_
#define CUTEFISH_COMPOSITOR_SHM_POOL_H_

#include "base/geometry.h"

#include <sys/uio.h>

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "render/texture.h"

namespace cutefish::compositor {

class ShmBuffer;

// Mapped onto the wl_shm errors by the protocol layer.
enum class ShmBufferError {
    None,
    InvalidFormat,
    // wl_shm has no separate error for bad dimensions or out-of-pool ranges.
    InvalidStride,
};

// Buffers hold a reference, so a client may destroy the wl_shm_pool while its
// wl_buffers are in use.
class ShmPool : public std::enable_shared_from_this<ShmPool>
{
public:
    ShmPool(const ShmPool &) = delete;
    ShmPool &operator=(const ShmPool &) = delete;
    ~ShmPool();

    // Takes ownership of `fd` in every case; a regular file shorter than `size`
    // is rejected.
    static std::shared_ptr<ShmPool> create(int fd, int32_t size);

    // Pools may only grow, which is what the protocol allows.
    bool resize(int32_t newSize);

    // The client can shrink the file at any time; between these calls SIGBUS is
    // caught, and endAccess() returns false. One pool per thread at a time.
    void beginAccess();
    bool endAccess();

    bool faulted() const { return m_faulted != 0; }

    // Null with `error` set when the range is not valid.
    std::shared_ptr<ShmBuffer> createBuffer(const base::Size &size, int32_t offset, int32_t stride,
                                            render::PixelFormat format, ShmBufferError *error);

    const uint8_t *data() const { return static_cast<const uint8_t *>(m_data); }

    size_t size() const { return m_size; }

    // Through the fd, so a file the client shrank fails cleanly instead of
    // faulting.
    bool write(size_t offset, const void *data, size_t length) const;

    // `stride` apart in the pool; the client's padding bytes are left alone.
    bool writeRows(size_t offset, size_t stride, size_t rows, size_t rowBytes,
                   const uint8_t *data, size_t dataStride) const;

private:
    ShmPool(int fd, void *data, size_t size);

    static void handleSigbus(int, siginfo_t *info, void *);

    bool fileCovers(size_t offset, size_t length) const;
    bool writeVectors(size_t offset, std::vector<iovec> vectors) const;

    int m_fd = -1;
    void *m_data = nullptr;
    size_t m_size = 0;
    // Written from the signal handler.
    volatile std::sig_atomic_t m_faulted = 0;
};

} // namespace cutefish::compositor

#endif // CUTEFISH_COMPOSITOR_SHM_POOL_H_
