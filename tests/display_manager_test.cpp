// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/display/manager/display_manager_unittest.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include <gtest/gtest.h>


#include "display/display_manager.h"

using namespace cutefish;

namespace {

display::DisplayId id(const char *value)
{
    return display::DisplayId(std::string(value));
}

display::DisplayMode mode(base::Size size)
{
    return {size, 60000, 148500, size.width() + 100, size.height() + 50, 0, "mode",
            true};
}

display::DisplaySnapshot physical(const char *name, base::Size size)
{
    display::DisplaySnapshot result;
    result.sessionId = id(name);
    result.persistentKey = "edid-" + std::string(name);
    result.portKey = "port-" + std::string(name);
    result.connectorName = std::string(name);
    result.devicePath = "/dev/dri/card0";
    result.physicalSizeMm = {600, 340};
    result.modes = {mode(size)};
    result.currentMode = result.modes.front();
    return result;
}

display::LogicalDisplay logical(const char *name, base::Rect bounds, bool primary)
{
    return {id(name),
            {id(name)},
            "Cutefish-" + std::string(name),
            bounds,
            bounds.adjusted(0, 24, 0, 0),
            {},
            display::DisplayRotation::Normal,
            primary};
}

} // namespace

TEST(DisplayManager, PublishesCoherentBatchesAndRejectsStaleState)
{
    display::DisplayManager manager;
    int started = 0;
    int changed = 0;
    const auto startedSubscription = manager.displayChangeStarted.connect([&] { ++started; });
    const auto changedSubscription =
            manager.displaysChanged.connect([&](uint64_t, uint64_t) { ++changed; });
    ASSERT_TRUE(manager.installHardware({"instance",
                                         "seat0",
                                         1,
                                         {physical("a", {100, 100}), physical("b", {120, 80})}}));
    ASSERT_TRUE(manager.installApplied(
            {"instance",
             1,
             1,
             {},
             {logical("a", {0, 0, 100, 100}, true), logical("b", {100, 20, 120, 80}, false)},
             {{id("b"), id("a"), display::LayoutEdge::Right, 20}}}));
    EXPECT_EQ(started, 2);
    EXPECT_EQ(changed, 2);
    EXPECT_EQ(manager.desktopBounds(), base::Rect(0, 0, 220, 100));

    display::DisplayError error;
    EXPECT_FALSE(manager.installApplied({"instance", 1, 1, {}, {}, {}}, &error));
    EXPECT_EQ(error.code, display::DisplayErrorCode::StaleConfiguration);
}

TEST(DisplayManager, SelectsDisplaysAcrossEdgesGapsAndSpanningWindows)
{
    display::DisplayManager manager;
    ASSERT_TRUE(manager.installHardware({"instance",
                                         "seat0",
                                         1,
                                         {physical("a", {100, 100}), physical("b", {100, 100})}}));
    ASSERT_TRUE(manager.installApplied(
            {"instance",
             1,
             1,
             {},
             {logical("a", {-100, 0, 100, 100}, false), logical("b", {20, 0, 100, 100}, true)},
             {}}));
    EXPECT_EQ(manager.displayAt(base::PointF(-0.001, 20))->id, id("a"));
    EXPECT_EQ(manager.displayAt(base::PointF(20, 20))->id, id("b"));
    EXPECT_EQ(manager.displayAt(base::PointF(10, 20)), nullptr);
    EXPECT_EQ(manager.nearestDisplay(base::PointF(18, 20))->id, id("b"));
    EXPECT_EQ(manager.displayForRect(base::Rect(-30, 10, 80, 40))->id, id("b"));
    EXPECT_EQ(manager.primaryDisplay()->id, id("b"));
}

TEST(DisplayManager, BuildsTheSharedTransformFromLogicalAndPhysicalState)
{
    display::DisplayManager manager;
    auto output = physical("a", {1920, 1080});
    ASSERT_TRUE(manager.installHardware(
            {"instance", "seat0", 1, {output}}));
    auto logicalOutput = logical("a", {-720, 40, 720, 1280}, true);
    logicalOutput.scale = *compositor::OutputScale::fromNumerator(180);
    logicalOutput.rotation = display::DisplayRotation::Rotate90;
    logicalOutput.workArea = logicalOutput.bounds;
    ASSERT_TRUE(manager.installApplied(
            {"instance", 1, 1, {output}, {logicalOutput}, {}}));
    const auto transform = manager.transformFor(id("a"));
    ASSERT_TRUE(transform);
    EXPECT_EQ(transform->logicalBounds(), logicalOutput.bounds);
    const base::PointF point(-500, 400);
    const base::PointF roundTrip = transform->nativeToGlobal(transform->globalToNative(point));
    EXPECT_NEAR(roundTrip.x(), point.x(), 0.001);
    EXPECT_NEAR(roundTrip.y(), point.y(), 0.001);
}

TEST(DisplayManager, TheTouchscreenFollowsTheInternalPanel)
{
    display::DisplayManager manager;
    auto external = physical("a", {1920, 1080});
    auto panel = physical("e", {2160, 1440});
    panel.connectionType = display::DisplayConnectionType::Internal;
    ASSERT_TRUE(manager.installHardware(
            {"instance", "seat0", 1, {external, panel}}));
    auto primary = logical("a", {0, 0, 1920, 1080}, true);
    auto internal = logical("e", {1920, 0, 1440, 960}, false);
    internal.scale = *compositor::OutputScale::fromNumerator(180);
    ASSERT_TRUE(manager.installApplied({"instance", 1, 1, {external, panel},
                                        {primary, internal}, {}}));

    const auto corner = manager.touchscreenToGlobal({0, 0});
    ASSERT_TRUE(corner);
    EXPECT_NEAR(corner->x(), 1920, 0.01);
    EXPECT_NEAR(corner->y(), 0, 0.01);
    const auto middle = manager.touchscreenToGlobal({0.5, 0.5});
    ASSERT_TRUE(middle);
    EXPECT_NEAR(middle->x(), 1920 + 720, 0.01);
    EXPECT_NEAR(middle->y(), 480, 0.01);
    EXPECT_TRUE(manager.touchscreenToGlobal({1, 1}));

    internal.rotation = display::DisplayRotation::Rotate90;
    internal.bounds = {1920, 0, 960, 1440};
    internal.workArea = internal.bounds;
    ASSERT_TRUE(manager.installApplied({"instance", 1, 2, {external, panel},
                                        {primary, internal}, {}}));
    const auto transform = manager.mappingFor(id("e"))->transform();
    for (const base::PointF normalized : {base::PointF(0.1, 0.2), base::PointF(0.8, 0.3)}) {
        const auto global = manager.touchscreenToGlobal(normalized);
        ASSERT_TRUE(global);
        EXPECT_TRUE(internal.bounds.contains(global->toPoint()));
        const base::PointF native = transform.logicalToNative(*global);
        EXPECT_NEAR(native.x(), normalized.x() * 2160, 0.5);
        EXPECT_NEAR(native.y(), normalized.y() * 1440, 0.5);
    }

    panel.connectionType = display::DisplayConnectionType::External;
    internal.rotation = display::DisplayRotation::Normal;
    internal.bounds = {1920, 0, 1440, 960};
    internal.workArea = internal.bounds;
    ASSERT_TRUE(manager.installApplied({"instance", 1, 3, {external, panel},
                                        {primary, internal}, {}}));
    const auto onPrimary = manager.touchscreenToGlobal({0.5, 0.5});
    ASSERT_TRUE(onPrimary);
    EXPECT_NEAR(onPrimary->x(), 960, 0.01);
    EXPECT_NEAR(onPrimary->y(), 540, 0.01);
}

TEST(DisplayManager, TheTouchscreenFollowsThePanelFitter)
{
    display::DisplayManager manager;
    auto panel = physical("e", {2160, 1440});
    panel.connectionType = display::DisplayConnectionType::Internal;
    panel.modes.push_back(mode({1920, 1080}));
    panel.modes.push_back(mode({1600, 1200}));
    panel.nativeMode = panel.modes[0];
    panel.currentMode = panel.modes[1];
    ASSERT_TRUE(manager.installHardware(
            {"instance", "seat0", 1, {panel}}));
    auto internal = logical("e", {0, 0, 1920, 1080}, true);
    ASSERT_TRUE(manager.installApplied(
            {"instance", 1, 1, {panel}, {internal}, {}}));

    const auto middle = manager.touchscreenToGlobal({0.5, 0.5});
    ASSERT_TRUE(middle);
    EXPECT_NEAR(middle->x(), 960, 0.01);
    EXPECT_NEAR(middle->y(), 540, 0.01);
    const auto nearBottom = manager.touchscreenToGlobal({0.25, (112.5 + 1215 * 0.9) / 1440});
    ASSERT_TRUE(nearBottom);
    EXPECT_NEAR(nearBottom->x(), 480, 0.01);
    EXPECT_NEAR(nearBottom->y(), 972, 0.01);
    const auto onBar = manager.touchscreenToGlobal({0.5, 0.02});
    ASSERT_TRUE(onBar);
    EXPECT_NEAR(onBar->y(), 0, 0.01);

    panel.currentMode = panel.modes[2];
    internal.bounds = {0, 0, 1600, 1200};
    internal.workArea = internal.bounds;
    ASSERT_TRUE(manager.installApplied(
            {"instance", 1, 2, {panel}, {internal}, {}}));
    const auto left = manager.touchscreenToGlobal({(120 + 1920 * 0.1) / 2160, 0.5});
    ASSERT_TRUE(left);
    EXPECT_NEAR(left->x(), 160, 0.01);
    EXPECT_NEAR(left->y(), 600, 0.01);
}
