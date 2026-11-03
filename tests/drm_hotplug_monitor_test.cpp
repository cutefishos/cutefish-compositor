// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/host/drm_display_host_manager_unittest.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: tests the bounded hotplug debounce.

#include <gtest/gtest.h>

#include "backend/drm/drm_hotplug_monitor.h"
#include "loop_test_util.h"

using namespace cutefish;

TEST(DrmHotplugMonitor, CoalescesEventBurst)
{
    backend::DrmHotplugMonitor monitor;
    int requested = 0;
    const auto subscription = monitor.reprobeRequested.connect([&] { ++requested; });
    monitor.notifyForTest();
    monitor.notifyForTest();
    monitor.notifyForTest();
    ASSERT_TRUE(test::runUntil([&] { return requested > 0; }, std::chrono::milliseconds(500)));
    test::runFor(std::chrono::milliseconds(800));
    EXPECT_EQ(requested, 1);
}

TEST(DrmHotplugMonitor, ContentProtectionDoesNotInvalidateOrReprobe)
{
    backend::DrmHotplugMonitor monitor;
    int resolved = 0;
    monitor.isContentProtectionProperty = [&](const std::string &devnode, uint32_t property) {
        ++resolved;
        EXPECT_EQ("/dev/dri/card1", devnode);
        EXPECT_EQ(829u, property);
        return true;
    };
    int invalidated = 0;
    int requested = 0;
    const auto invalidation = monitor.snapshotsInvalidated.connect([&] { ++invalidated; });
    const auto reprobe = monitor.reprobeRequested.connect([&] { ++requested; });
    monitor.notifyForTest("/dev/dri/card1", "829");
    test::runFor(std::chrono::milliseconds(800));
    EXPECT_EQ(1, resolved);
    EXPECT_EQ(0, invalidated);
    EXPECT_EQ(0, requested);
}

TEST(DrmHotplugMonitor, UnresolvedEventsStillInvalidateAndReprobe)
{
    // A false resolution includes unknown IDs, link-status and absent devices.
    struct Event { const char *node; const char *property; bool resolves; };
    const Event events[] = {
        {"/dev/dri/card1", nullptr, false},
        {"/dev/dri/card1", "", false},
        {"/dev/dri/card1", "not-a-number", false},
        {"/dev/dri/card1", "829junk", false},
        {"/dev/dri/card1", "-1", false},
        {"/dev/dri/card1", "4294967296", false},
        {"/dev/dri/card1", "999", true},
        {"/dev/dri/card9", "829", true},
        {nullptr, "829", false},
        {"", "829", false},
    };
    for (const auto &event : events) {
        SCOPED_TRACE(event.property ? event.property : "missing PROPERTY");
        backend::DrmHotplugMonitor monitor;
        int resolved = 0;
        monitor.isContentProtectionProperty = [&](const std::string &, uint32_t) {
            ++resolved;
            return false;
        };
        int invalidated = 0;
        int requested = 0;
        const auto invalidation = monitor.snapshotsInvalidated.connect([&] { ++invalidated; });
        const auto reprobe = monitor.reprobeRequested.connect([&] { ++requested; });
        monitor.notifyForTest(event.node, event.property);
        EXPECT_EQ(event.resolves ? 1 : 0, resolved);
        EXPECT_EQ(1, invalidated);
        ASSERT_TRUE(test::runUntil([&] { return requested == 1; }, std::chrono::milliseconds(500)));
    }
}

TEST(DrmHotplugMonitor, MissingResolverStillReprobes)
{
    backend::DrmHotplugMonitor monitor;
    int requested = 0;
    const auto reprobe = monitor.reprobeRequested.connect([&] { ++requested; });
    monitor.notifyForTest("/dev/dri/card1", "829");
    ASSERT_TRUE(test::runUntil([&] { return requested == 1; }, std::chrono::milliseconds(500)));
}

TEST(DrmHotplugMonitor, ContentProtectionDoesNotCancelPendingTopologyReprobe)
{
    backend::DrmHotplugMonitor monitor;
    monitor.isContentProtectionProperty = [](const std::string &, uint32_t) { return true; };
    int requested = 0;
    int invalidated = 0;
    const auto reprobe = monitor.reprobeRequested.connect([&] { ++requested; });
    const auto invalidation = monitor.snapshotsInvalidated.connect([&] { ++invalidated; });
    monitor.notifyForTest();
    monitor.notifyForTest("/dev/dri/card1", "829");
    ASSERT_TRUE(test::runUntil([&] { return requested == 1; }, std::chrono::milliseconds(500)));
    EXPECT_EQ(1, invalidated);
}
