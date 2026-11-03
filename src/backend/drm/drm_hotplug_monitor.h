// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/host/drm_display_host_manager.cc
//           chromium/src/ui/ozone/platform/drm/gpu/drm_gpu_display_manager.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: monitor DRM udev events on the loop and coalesce dock bursts
// behind a bounded reprobe deadline.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "base/signal.h"
#include "base/timer.h"

struct udev;
struct udev_monitor;

namespace cutefish::backend {

class DrmHotplugMonitor final
{
public:
    DrmHotplugMonitor();
    ~DrmHotplugMonitor();

    DrmHotplugMonitor(const DrmHotplugMonitor &) = delete;
    DrmHotplugMonitor &operator=(const DrmHotplugMonitor &) = delete;

    bool start();
    void notifyForTest(const char *devnode = nullptr, const char *property = nullptr);

    // Resolve using an already-open backend fd; never open a primary node here.
    // Only a positively identified Content Protection property skips reprobe.
    std::function<bool(const std::string &devnode, uint32_t property)> isContentProtectionProperty;

    base::Signal<> snapshotsInvalidated;
    base::Signal<> reprobeRequested;

private:
    void onReadable();
    void notify(const char *devnode, const char *property);
    void schedule();
    void fire();

    udev *m_udev = nullptr;
    udev_monitor *m_monitor = nullptr;
    int m_watch = 0;
    base::Timer m_quiet;
    base::Timer m_deadline;
};

} // namespace cutefish::backend
