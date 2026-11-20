// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/gbm_pixmap.h
//           (GbmPixmap::framebuffer(), the framebuffer cached on the buffer)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_RENDER_SCANOUT_H_
#define CUTEFISH_RENDER_SCANOUT_H_

#include <memory>
#include <utility>
#include <vector>

#include "render/dmabuf.h"
#include "render/sync_fence.h"

namespace cutefish::render {

// Importing for scanout must not disturb the renderer's EGLImage import or the
// plane fds either import holds.
class ScanoutBuffer
{
public:
    // Cached on the buffer and torn down with it.
    class Import
    {
    public:
        virtual ~Import() = default;
    };

    virtual ~ScanoutBuffer() = default;

    // The DMA-BUF description, plane fds included.
    virtual const DmaBufAttributes& scanoutAttributes() const = 0;

    // Null if none; `device` is any stable pointer identifying the importer.
    Import* scanoutImport(const void* device) const
    {
        for (const auto& entry : m_imports) {
            if (entry.first == device)
                return entry.second.get();
        }
        return nullptr;
    }

    // A null import records that the device could not import this buffer.
    Import* setScanoutImport(const void* device, std::unique_ptr<Import> import)
    {
        Import* raw = import.get();
        for (auto& entry : m_imports) {
            if (entry.first == device) {
                entry.second = std::move(import);
                return raw;
            }
        }
        m_imports.emplace_back(device, std::move(import));
        return raw;
    }

    bool scanoutImportFailed(const void* device) const
    {
        for (const auto& entry : m_imports) {
            if (entry.first == device)
                return entry.second == nullptr;
        }
        return false;
    }

    // Releasing the client's storage waits for the matching release.
    virtual void onScanoutAcquired() { }
    virtual void onScanoutReleased(SyncFence fence) { (void)fence; }

private:
    std::vector<std::pair<const void*, std::unique_ptr<Import>>> m_imports;
};

} // namespace cutefish::render

#endif // CUTEFISH_RENDER_SCANOUT_H_
