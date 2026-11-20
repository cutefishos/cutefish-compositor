// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/gfx/gpu_fence_handle.h
//           chromium/src/ui/ozone/platform/drm/gpu/
//               hardware_display_plane_manager_atomic.cc
//               (CreateMergedGpuFenceFromFDs)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_RENDER_SYNC_FENCE_H_
#define CUTEFISH_RENDER_SYNC_FENCE_H_

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "core/scoped_fd.h"

namespace cutefish::render {

// An invalid fence means nothing to wait for and counts as signalled.
class SyncFence
{
public:
    SyncFence() = default;

    explicit SyncFence(ScopedFd fd)
        : m_fd(std::move(fd))
    {
    }

    SyncFence(SyncFence&&) noexcept = default;
    SyncFence& operator=(SyncFence&&) noexcept = default;

    bool isValid() const { return m_fd.isValid(); }

    int get() const { return m_fd.get(); }

    int release() { return m_fd.release(); }

    void reset() { m_fd.reset(); }

    // A second reference to the same fence. Invalid fences duplicate as invalid.
    SyncFence duplicate() const;

    // Never blocks; an invalid fence counts as signalled.
    bool isSignaled() const;

    // In nanoseconds of CLOCK_MONOTONIC, the latest of its fences; empty while
    // unsignalled or for anything but a sync_file.
    std::optional<int64_t> signalTimeNs() const;

    // `timeoutMs` -1 waits forever.
    bool wait(int timeoutMs = -1) const;

    // Invalid inputs are skipped, so merging nothing yields an invalid fence.
    static SyncFence merge(std::vector<SyncFence> fences);

private:
    ScopedFd m_fd;
};

} // namespace cutefish::render

#endif // CUTEFISH_RENDER_SYNC_FENCE_H_
