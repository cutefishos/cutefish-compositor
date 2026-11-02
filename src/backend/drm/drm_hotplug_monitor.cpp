// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/host/drm_display_host_manager.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: one udev monitor closes the subscribe-before-query race
// and turns a connector event burst into one bounded reprobe request.

#include "backend/drm/drm_hotplug_monitor.h"

#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <libudev.h>
#include <string_view>
#include <sys/epoll.h>
#include <unistd.h>
#include <xf86drmMode.h>

#include "base/event_loop.h"
#include "core/main_loop.h"

namespace cutefish::backend {
namespace {

// HDCP state changes are connector property notifications, not cable events.
// Keep unknown properties (especially link-status) on the recovery path.
bool isContentProtectionEvent(udev_device *device)
{
    const char *property = udev_device_get_property_value(device, "PROPERTY");
    const char *node = udev_device_get_devnode(device);
    if (!property || !node)
        return false;
    uint32_t id = 0;
    const std::string_view text(property);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size())
        return false;
    const int fd = open(node, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    drmModePropertyPtr info = drmModeGetProperty(fd, id);
    const bool contentProtection = info && std::strcmp(info->name, "Content Protection") == 0;
    if (info)
        drmModeFreeProperty(info);
    close(fd);
    return contentProtection;
}

} // namespace

DrmHotplugMonitor::DrmHotplugMonitor()
    : m_quiet(core::mainLoop())
    , m_deadline(core::mainLoop())
{
    m_quiet.setSingleShot(true);
    m_deadline.setSingleShot(true);
    m_quiet.onTimeout = [this] { fire(); };
    m_deadline.onTimeout = [this] { fire(); };
}

DrmHotplugMonitor::~DrmHotplugMonitor()
{
    if (m_watch)
        core::mainLoop()->unwatch(m_watch);
    if (m_monitor)
        udev_monitor_unref(m_monitor);
    if (m_udev)
        udev_unref(m_udev);
}

bool DrmHotplugMonitor::start()
{
    if (m_monitor)
        return true;
    m_udev = udev_new();
    if (!m_udev)
        return false;
    m_monitor = udev_monitor_new_from_netlink(m_udev, "udev");
    if (!m_monitor || udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "drm", nullptr) < 0
        || udev_monitor_enable_receiving(m_monitor) < 0) {
        return false;
    }
    m_watch = core::mainLoop()->watch(udev_monitor_get_fd(m_monitor), EPOLLIN,
                                      [this](uint32_t) { onReadable(); });
    return m_watch != 0;
}

void DrmHotplugMonitor::notifyForTest()
{
    schedule();
}

void DrmHotplugMonitor::onReadable()
{
    while (udev_device *device = udev_monitor_receive_device(m_monitor)) {
        const bool contentProtection = isContentProtectionEvent(device);
        udev_device_unref(device);
        if (!contentProtection)
            schedule();
    }
}

void DrmHotplugMonitor::schedule()
{
    snapshotsInvalidated();
    m_quiet.start(std::chrono::milliseconds(100));
    if (!m_deadline.isActive())
        m_deadline.start(std::chrono::milliseconds(750));
}

void DrmHotplugMonitor::fire()
{
    if (!m_quiet.isActive() && !m_deadline.isActive())
        return;
    m_quiet.stop();
    m_deadline.stop();
    reprobeRequested();
}

} // namespace cutefish::backend
