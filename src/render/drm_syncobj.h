// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RENDER_DRM_SYNCOBJ_H_
#define CUTEFISH_RENDER_DRM_SYNCOBJ_H_

#include <cstdint>
#include <memory>

#include "core/scoped_fd.h"
#include "render/sync_fence.h"

namespace cutefish::render {

class DrmSyncobj
{
public:
    DrmSyncobj(const DrmSyncobj&) = delete;
    DrmSyncobj& operator=(const DrmSyncobj&) = delete;
    ~DrmSyncobj();

    // `drmFd` must stay open for the returned object's lifetime; null when the
    // fd is not a syncobj.
    static std::shared_ptr<DrmSyncobj> import(int drmFd, ScopedFd syncobjFd);

    static std::shared_ptr<DrmSyncobj> create(int drmFd);

    bool isMaterialized(uint64_t point) const;

    // Invalid when the point has no fence yet, which means synchronize some
    // other way, not signalled.
    SyncFence exportFence(uint64_t point) const;

    // Readable once `point` signals, also when the fence is submitted later;
    // invalid when the kernel cannot.
    ScopedFd signalEventFd(uint64_t point) const;

    // Immediately when `fence` is invalid, otherwise when it signals; false if
    // the kernel rejected it.
    bool signal(uint64_t point, const SyncFence& fence);

private:
    DrmSyncobj(int drmFd, uint32_t handle);

    int m_drmFd = -1;
    uint32_t m_handle = 0;
};

} // namespace cutefish::render

#endif // CUTEFISH_RENDER_DRM_SYNCOBJ_H_
