// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "backend/drm/gpu_boost_policy.h"

using cutefish::backend::GpuBoostPolicy;
using namespace std::chrono_literals;

TEST(GpuBoostPolicyTest, NothingIsHeldBeforeAFrame)
{
    GpuBoostPolicy policy;
    EXPECT_FALSE(policy.holds(GpuBoostPolicy::Time {}));
    EXPECT_FALSE(policy.releaseTime());
}

TEST(GpuBoostPolicyTest, OneOffRedrawsHoldNothing)
{
    GpuBoostPolicy policy;
    // A text cursor blinking, then a clock ticking.
    auto now = GpuBoostPolicy::Time {};
    for (int i = 0; i < 10; ++i) {
        policy.compositorFrame(now);
        EXPECT_FALSE(policy.holds(now));
        now += 500ms;
    }
    policy.compositorFrame(now + 60s);
    EXPECT_FALSE(policy.holds(now + 60s));
    EXPECT_FALSE(policy.releaseTime());
}

TEST(GpuBoostPolicyTest, ASecondFrameSoonAfterStartsTheHold)
{
    GpuBoostPolicy policy;
    const GpuBoostPolicy::Time start {};
    policy.compositorFrame(start);
    EXPECT_FALSE(policy.holds(start));
    const auto second = start + GpuBoostPolicy::kStreakGap;
    policy.compositorFrame(second);
    EXPECT_TRUE(policy.holds(second));
    EXPECT_EQ(policy.releaseTime(), second + GpuBoostPolicy::kLinger);
}

TEST(GpuBoostPolicyTest, FramesHoldUntilTheyStopForTheLinger)
{
    GpuBoostPolicy policy;
    auto now = GpuBoostPolicy::Time {};
    policy.compositorFrame(now);
    // Frames at a throttled clock come a few refreshes apart; each one moves
    // the release on.
    for (int i = 0; i < 40; ++i) {
        now += 50ms;
        policy.compositorFrame(now);
        EXPECT_TRUE(policy.holds(now));
    }
    EXPECT_EQ(policy.releaseTime(), now + GpuBoostPolicy::kLinger);
    EXPECT_TRUE(policy.holds(now + GpuBoostPolicy::kLinger - 1ms));
    EXPECT_FALSE(policy.holds(now + GpuBoostPolicy::kLinger));
}

TEST(GpuBoostPolicyTest, AFrameWhileHeldKeepsTheHold)
{
    GpuBoostPolicy policy;
    const GpuBoostPolicy::Time start {};
    policy.compositorFrame(start);
    policy.compositorFrame(start + 20ms);
    // An animation following the last one closely, after a gap the streak
    // alone would not bridge.
    const auto next = start + 20ms + GpuBoostPolicy::kLinger / 2;
    policy.compositorFrame(next);
    EXPECT_TRUE(policy.holds(start + 20ms + GpuBoostPolicy::kLinger));
    EXPECT_FALSE(policy.holds(next + GpuBoostPolicy::kLinger));
}

TEST(GpuBoostPolicyTest, AnInactiveSessionReleasesAndIgnoresFrames)
{
    GpuBoostPolicy policy;
    const GpuBoostPolicy::Time start {};
    policy.compositorFrame(start);
    policy.compositorFrame(start + 10ms);
    ASSERT_TRUE(policy.holds(start + 10ms));
    policy.setActive(false);
    EXPECT_FALSE(policy.holds(start + 10ms));
    EXPECT_FALSE(policy.releaseTime());
    policy.compositorFrame(start + 20ms);
    policy.compositorFrame(start + 30ms);
    EXPECT_FALSE(policy.holds(start + 30ms));
    // Back on the seat, a streak starts over.
    policy.setActive(true);
    policy.compositorFrame(start + 40ms);
    EXPECT_FALSE(policy.holds(start + 40ms));
    policy.compositorFrame(start + 50ms);
    EXPECT_TRUE(policy.holds(start + 50ms));
}

TEST(GpuBoostPolicyTest, AnExpectedAnimationHoldsBeforeItsFirstFrame)
{
    GpuBoostPolicy policy;
    const GpuBoostPolicy::Time start {};
    policy.expectAnimation(start);
    EXPECT_TRUE(policy.holds(start));
    EXPECT_EQ(policy.releaseTime(), start + GpuBoostPolicy::kLinger);
    // Its frames carry the hold on.
    policy.compositorFrame(start + 300ms);
    EXPECT_TRUE(policy.holds(start + GpuBoostPolicy::kLinger));
    policy.setActive(false);
    policy.expectAnimation(start + 400ms);
    EXPECT_FALSE(policy.holds(start + 400ms));
}
