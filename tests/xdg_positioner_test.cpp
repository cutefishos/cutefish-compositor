// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/wayland/wayland_positioner_unittest.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include <gtest/gtest.h>
#include <xdg-shell-server-protocol.h>

#include "base/geometry.h"
#include "wayland/protocols/xdg_positioner.h"

using cutefish::base::Point;
using cutefish::base::Rect;
using cutefish::base::Size;
using cutefish::wayland::WaylandPositioner;

namespace {

// By default the cases happen on a 5x5 grid with an anchor rect at (2,2,1x1).
struct TestCaseBuilder {
    WaylandPositioner positioner;
    Rect workArea {0, 0, 5, 5};

    TestCaseBuilder() { positioner.setAnchorRect({2, 2, 1, 1}); }

    TestCaseBuilder& setAnchor(uint32_t anchor)
    {
        positioner.setAnchor(anchor);
        return *this;
    }

    TestCaseBuilder& setGravity(uint32_t gravity)
    {
        positioner.setGravity(gravity);
        return *this;
    }

    TestCaseBuilder& setAdjustment(uint32_t adjustment)
    {
        positioner.setAdjustment(adjustment);
        return *this;
    }

    TestCaseBuilder& setAnchorRect(int x, int y, int w, int h)
    {
        positioner.setAnchorRect({x, y, w, h});
        return *this;
    }

    TestCaseBuilder& setWorkArea(const Rect& rect)
    {
        workArea = rect;
        return *this;
    }

    TestCaseBuilder& setSize(int w, int h)
    {
        positioner.setSize({w, h});
        return *this;
    }

    WaylandPositioner::Result solve() const { return positioner.calculateBounds(workArea); }

    Rect solveToRect() const { return solve().toRect(); }
};

TEST(WaylandPositionerTest, UnconstrainedCases)
{
    EXPECT_EQ(TestCaseBuilder().setSize(1, 1).solveToRect(), Rect(2, 2, 1, 1));

    EXPECT_EQ(TestCaseBuilder().setSize(2, 1).setAnchor(XDG_POSITIONER_ANCHOR_RIGHT).solveToRect(),
              Rect(2, 2, 2, 1));
    EXPECT_EQ(TestCaseBuilder().setSize(2, 1).setAnchor(XDG_POSITIONER_ANCHOR_LEFT).solveToRect(),
              Rect(1, 2, 2, 1));

    EXPECT_EQ(TestCaseBuilder()
                  .setSize(1, 2)
                  .setAnchorRect(2, 2, 0, 0)
                  .setGravity(XDG_POSITIONER_GRAVITY_TOP)
                  .solveToRect(),
              Rect(2, 0, 1, 2));
    EXPECT_EQ(TestCaseBuilder()
                  .setSize(1, 2)
                  .setAnchorRect(2, 2, 0, 0)
                  .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM)
                  .solveToRect(),
              Rect(2, 2, 1, 2));

    EXPECT_EQ(TestCaseBuilder()
                  .setSize(2, 2)
                  .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM_LEFT)
                  .setAnchor(XDG_POSITIONER_ANCHOR_BOTTOM_LEFT)
                  .solveToRect(),
              Rect(0, 3, 2, 2));

    EXPECT_EQ(TestCaseBuilder()
                  .setSize(2, 2)
                  .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM_LEFT)
                  .setAnchor(XDG_POSITIONER_ANCHOR_TOP_RIGHT)
                  .solveToRect(),
              Rect(1, 2, 2, 2));
}

TEST(WaylandPositionerTest, FlipSlideResizePriority)
{
    TestCaseBuilder builder;
    builder.setAnchorRect(4, 4, 0, 0)
        .setSize(2, 2)
        .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT)
        .setAnchor(XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
    EXPECT_EQ(builder.setAdjustment(~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE).solveToRect(),
              Rect(2, 2, 2, 2));
    EXPECT_EQ(builder.setAdjustment(~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X).solveToRect(),
              Rect(3, 2, 2, 2));
    EXPECT_EQ(builder.setAdjustment(~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y).solveToRect(),
              Rect(2, 3, 2, 2));
    EXPECT_EQ(builder
                  .setAdjustment(XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X
                                 | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y)
                  .solveToRect(),
              Rect(4, 4, 1, 1));
}

TEST(WaylandPositionerTest, TriesToMaximizeArea)
{
    const WaylandPositioner::Result result =
        TestCaseBuilder()
            .setAnchorRect(2, 4, 0, 0)
            .setSize(4, 10)
            .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT)
            .setAnchor(XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT)
            .setAdjustment(~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE)
            .solve();
    EXPECT_EQ(result.origin, Point(1, 0));
    EXPECT_EQ(result.size, Size(4, 5));
}

TEST(WaylandPositionerTest, PropagatesAnInitialFlip)
{
    const WaylandPositioner::Result result =
        TestCaseBuilder()
            .setAnchorRect(3, 1, 0, 0)
            .setSize(2, 2)
            .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT)
            .setAnchor(XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT)
            .setAdjustment(~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE)
            .solve();
    EXPECT_EQ(result.origin, Point(3, 1));
    EXPECT_EQ(result.size, Size(2, 2));
}

TEST(WaylandPositionerTest, PreventsSlidingThatOccludesAnchorRect)
{
    EXPECT_EQ(TestCaseBuilder()
                  .setSize(3, 3)
                  .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT)
                  .setAnchor(XDG_POSITIONER_ANCHOR_BOTTOM_LEFT)
                  .setAdjustment(~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE)
                  .solveToRect(),
              Rect(2, 3, 3, 2));

    EXPECT_EQ(TestCaseBuilder()
                  .setSize(4, 4)
                  .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT)
                  .setAnchor(XDG_POSITIONER_ANCHOR_TOP_LEFT)
                  .setAdjustment(~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE)
                  .solveToRect(),
              Rect(1, 1, 4, 4));
}

TEST(WaylandPositionerTest, AllowsSlidingThatOccludesWhenThereAreNoOtherOptions)
{
    EXPECT_EQ(TestCaseBuilder()
                  .setSize(4, 4)
                  .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT)
                  .setAnchor(XDG_POSITIONER_ANCHOR_BOTTOM_LEFT)
                  .setAdjustment(~(XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X
                                   | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y))
                  .solveToRect(),
              Rect(1, 1, 4, 4));
}

TEST(WaylandPositionerTest, ResizableShouldNotBeEmpty)
{
    EXPECT_EQ(TestCaseBuilder()
                  .setSize(3, 3)
                  .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM)
                  .setAnchor(XDG_POSITIONER_ANCHOR_BOTTOM)
                  .setAdjustment(~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE)
                  .setAnchorRect(1, -10, 4, 4)
                  .solveToRect(),
              Rect(2, 0, 3, 1));
    EXPECT_EQ(TestCaseBuilder()
                  .setSize(3, 3)
                  .setGravity(XDG_POSITIONER_GRAVITY_RIGHT)
                  .setAnchor(XDG_POSITIONER_ANCHOR_RIGHT)
                  .setAdjustment(~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE)
                  .setAnchorRect(-10, 2, 4, 4)
                  .solveToRect(),
              Rect(0, 2, 1, 3));
}

TEST(WaylandPositionerTest, AllowsAdditionalAdjustmentsIfNoSolutionCanBeFound)
{
    EXPECT_EQ(TestCaseBuilder()
                  .setWorkArea(Rect(0, 0, 5, 5))
                  .setSize(10, 10)
                  .setAnchorRect(0, 0, 0, 0)
                  .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT)
                  .setAdjustment(XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X
                                 | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y)
                  .solveToRect(),
              Rect(0, 0, 5, 5));
}

TEST(WaylandPositionerTest, WorkAreaWithANonZeroOrigin)
{
    EXPECT_EQ(TestCaseBuilder()
                  .setWorkArea(Rect(100, 200, 5, 5))
                  .setAnchorRect(104, 204, 0, 0)
                  .setSize(2, 2)
                  .setGravity(XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT)
                  .setAnchor(XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT)
                  .setAdjustment(~XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE)
                  .solveToRect(),
              Rect(102, 202, 2, 2));
}

TEST(WaylandPositionerTest, OffsetIsAppliedBeforeTheConstraintsAre)
{
    EXPECT_EQ(TestCaseBuilder().setSize(1, 1).solveToRect(), Rect(2, 2, 1, 1));

    WaylandPositioner positioner;
    positioner.setAnchorRect({2, 2, 1, 1});
    positioner.setSize({1, 1});
    positioner.setOffset({1, -1});
    EXPECT_EQ(positioner.calculateBounds(Rect(0, 0, 5, 5)).toRect(), Rect(3, 1, 1, 1));
}

} // namespace
