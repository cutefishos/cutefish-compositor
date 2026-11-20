// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/buffer.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WAYLAND_PROTOCOLS_LINUX_DRM_SYNCOBJ_H_
#define CUTEFISH_WAYLAND_PROTOCOLS_LINUX_DRM_SYNCOBJ_H_

#include <wayland-server-core.h>

#include <cstdint>

namespace cutefish::wayland {

constexpr uint32_t kLinuxDrmSyncobjVersion = 1;

// `drmFd` is the device the client's timelines are imported onto. Null when it is not a usable DRM device.
wl_global* createLinuxDrmSyncobjGlobal(wl_display* display, int drmFd);

// A wp_linux_drm_syncobj_surface_v1 error code, or -1. `conflicting` is an acquire point at or after
// the release point on the same timeline.
struct SyncobjCommit {
    bool buffer = false;
    bool explicitSyncBuffer = false;
    bool acquire = false;
    bool release = false;
    bool conflicting = false;
};

int syncobjCommitError(const SyncobjCommit& commit);

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PROTOCOLS_LINUX_DRM_SYNCOBJ_H_
