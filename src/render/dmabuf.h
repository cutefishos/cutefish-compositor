// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/gfx/native_pixmap_handle.h
//           chromium/src/gpu/command_buffer/common/capabilities.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_RENDER_DMABUF_H_
#define CUTEFISH_RENDER_DMABUF_H_

#include <sys/types.h>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>


#include "core/scoped_fd.h"
#include "render/texture.h"
#include "base/geometry.h"

namespace cutefish::render {

// linux-dmabuf allows at most four planes per buffer.
constexpr size_t kMaxDmaBufPlanes = 4;

struct DmaBufPlane {
    ScopedFd fd;
    uint32_t offset = 0;
    uint32_t stride = 0;
};

// One modifier shared by every plane; kept whole so the same description can go
// to KMS for scanout.
struct DmaBufAttributes {
    base::Size size;
    uint32_t format = 0; // DRM fourcc
    uint64_t modifier = 0;
    bool yInvert = false;
    std::vector<DmaBufPlane> planes;
};

// DRM_FORMAT_MOD_INVALID stands for the implicit modifier.
using DmaBufFormats = std::map<uint32_t, std::vector<uint64_t>>;

// Null in tests and headless runs, in which case no linux-dmabuf global is
// advertised.
class DmaBufDevice
{
public:
    virtual ~DmaBufDevice() = default;

    virtual const DmaBufFormats& dmaBufFormats() const = 0;

    // 0 when it could not be determined, which downgrades the advertised
    // protocol version.
    virtual dev_t dmaBufMainDevice() const = 0;

    // The plane fds must outlive the returned texture; null when the import
    // fails.
    virtual std::shared_ptr<Texture> importDmaBuf(const DmaBufAttributes& attributes) = 0;
};

} // namespace cutefish::render

#endif // CUTEFISH_RENDER_DMABUF_H_
