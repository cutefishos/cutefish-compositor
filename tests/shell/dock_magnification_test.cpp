// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "shell/dock/dock_magnification.h"
#include "shell/dock/dock_presentation.h"

using namespace cutefish;
using shell::DockMagnification;
using shell::DockMagnifier;

namespace {

constexpr double kSlot = 70;
constexpr double kPeak = 96.0 / 54;
constexpr double kFrameMs = shell::DockPresentation::kFrameIntervalMs;

std::vector<double> edges(double start, int count)
{
    std::vector<double> result;
    for (int i = 0; i <= count; ++i)
        result.push_back(start + i * kSlot);
    return result;
}

// Returns the time it took, at most `limitMs`.
double settle(DockMagnifier &magnifier, double &nowMs, double limitMs = 1000)
{
    const double start = nowMs;
    while (magnifier.isMoving() && nowMs - start < limitMs) {
        nowMs += kFrameMs;
        magnifier.advance(nowMs);
    }
    return nowMs - start;
}

} // namespace

TEST(DockMagnificationTest, NoAmountIsTheRestLayoutExactly)
{
    const DockMagnification none(331.7, 0, kPeak, kSlot);
    EXPECT_TRUE(none.isIdentity());
    EXPECT_TRUE(DockMagnification().isIdentity());
    for (double position : {-1e6, 0.0, 0.1, 17.25, 331.7, 1234.5678})
        EXPECT_EQ(position, none.map(position));
    EXPECT_TRUE(DockMagnification(331.7, 1, 1, kSlot).isIdentity());
}

TEST(DockMagnificationTest, TheSlotUnderThePointerIsThePeakAndStaysUnderIt)
{
    for (double amount : {0.25, 0.5, 1.0}) {
        const double focus = 500.3;
        const DockMagnification field(focus, amount, kPeak, kSlot);
        EXPECT_NEAR(focus, field.map(focus), 1e-12);
        const double length = field.map(focus + kSlot / 2) - field.map(focus - kSlot / 2);
        EXPECT_NEAR(1 + amount * (kPeak - 1), length / kSlot, 1e-12) << amount;
    }
}

TEST(DockMagnificationTest, NeighboursFallOffOverThreeAndAHalfSlots)
{
    const double focus = 1000;
    const DockMagnification field(focus, 1, kPeak, kSlot);
    double previous = kPeak;
    for (int slot = 1; slot < 8; ++slot) {
        const double from = focus + (slot - 0.5) * kSlot;
        const double scale = (field.map(from + kSlot) - field.map(from)) / kSlot;
        if (slot < 4) {
            EXPECT_LT(scale, previous) << slot;
            EXPECT_GT(scale, 1) << slot;
        } else {
            EXPECT_NEAR(1, scale, 1e-12) << slot;
        }
        const double mirrored = (field.map(2 * focus - from) - field.map(2 * focus - from - kSlot)) / kSlot;
        EXPECT_NEAR(scale, mirrored, 1e-12) << slot;
        previous = scale;
    }
}

TEST(DockMagnificationTest, OrderIsKeptAndTheRowGrowsByAConstantAwayFromItsEnds)
{
    const std::vector<double> row = edges(100, 20);
    const double length = row.back() - row.front();
    const double radius = DockMagnification::kRadiusSlots * kSlot;
    for (double focus = row.front(); focus <= row.back(); focus += 3.7) {
        const DockMagnification field(focus, 1, kPeak, kSlot);
        for (size_t i = 1; i < row.size(); ++i)
            EXPECT_GT(field.map(row[i]), field.map(row[i - 1]));
        const double grown = field.map(row.back()) - field.map(row.front()) - length;
        if (focus - row.front() >= radius && row.back() - focus >= radius)
            EXPECT_NEAR(field.expansion(), grown, 1e-9) << focus;
        else
            EXPECT_LE(grown, field.expansion() + 1e-9) << focus;
        EXPECT_LE(field.map(row.front()), row.front());
        EXPECT_GE(field.map(row.back()), row.back());
    }
}

TEST(DockMagnificationTest, SmallPointerStepsMoveEverythingALittle)
{
    // Across slot edges and past both ends, nothing jumps: a step of the
    // focus moves any position by at most the step times the profile's height.
    const std::vector<double> row = edges(100, 8);
    const double step = 0.05;
    const double height = DockMagnification(0, 1, kPeak, kSlot).expansion()
            / (DockMagnification::kRadiusSlots * kSlot);
    for (double focus = 0; focus < 800; focus += step) {
        const DockMagnification before(focus, 1, kPeak, kSlot);
        const DockMagnification after(focus + step, 1, kPeak, kSlot);
        for (double edge : row)
            EXPECT_LE(std::abs(after.map(edge) - before.map(edge)), height * step + 1e-9) << focus << " " << edge;
    }
    for (double amount = 0; amount < 1; amount += 0.01) {
        const DockMagnification before(400, amount, kPeak, kSlot);
        const DockMagnification after(400, amount + 0.01, kPeak, kSlot);
        for (double edge : row)
            EXPECT_LE(std::abs(after.map(edge) - before.map(edge)), height * 0.01 * kSlot * 3.5 + 1e-9);
    }
}

TEST(DockMagnifierTest, FadesInAndOutSoftlyToExactEnds)
{
    DockMagnifier magnifier;
    EXPECT_FALSE(magnifier.track(100));
    magnifier.setEnabled(true);
    ASSERT_TRUE(magnifier.track(100));
    EXPECT_EQ(100, magnifier.focus());
    EXPECT_FALSE(magnifier.track(100));

    double now = 1000;
    double previous = 0;
    double reached = -1;
    int frames = 0;
    while (magnifier.isMoving()) {
        now += kFrameMs;
        magnifier.advance(now);
        ++frames;
        EXPECT_GT(magnifier.amount(), previous);
        EXPECT_LE(magnifier.amount(), 1);
        if (reached < 0 && magnifier.amount() == 1)
            reached = now - 1000;
        previous = magnifier.amount();
        if (frames == 1)
            EXPECT_LT(magnifier.amount(), 0.05);
    }
    EXPECT_EQ(1, magnifier.amount());
    EXPECT_LE(reached, DockMagnifier::kAmountRampMs + kFrameMs);
    EXPECT_GE(reached, DockMagnifier::kAmountRampMs);

    ASSERT_TRUE(magnifier.track(180.5));
    EXPECT_EQ(180.5, magnifier.focus());
    EXPECT_FALSE(magnifier.isMoving());

    ASSERT_TRUE(magnifier.leave());
    EXPECT_LE(settle(magnifier, now), DockMagnifier::kAmountRampMs + kFrameMs);
    EXPECT_EQ(0, magnifier.amount());
    EXPECT_EQ(180.5, magnifier.focus());

    magnifier.setEnabled(false);
    EXPECT_FALSE(magnifier.track(100));
    EXPECT_EQ(0, magnifier.amount());
}

TEST(DockMagnifierTest, TurningBackMidFadeGoesBackFromWhereItIs)
{
    DockMagnifier magnifier;
    magnifier.setEnabled(true);
    magnifier.track(100);
    double now = 1000;
    for (int i = 0; i < 4; ++i)
        magnifier.advance(now += kFrameMs);
    const double rising = magnifier.amount();
    ASSERT_GT(rising, 0.2);
    ASSERT_LT(rising, 0.8);
    magnifier.leave();
    // From there, by no more than a frame's worth of the steepest part, and
    // only down.
    const double steepest = 1.5 * kFrameMs / DockMagnifier::kAmountRampMs;
    double previous = rising;
    while (magnifier.isMoving()) {
        magnifier.advance(now += kFrameMs);
        EXPECT_LT(magnifier.amount(), previous);
        EXPECT_LE(previous - magnifier.amount(), steepest + 1e-12);
        previous = magnifier.amount();
    }
    EXPECT_EQ(0, magnifier.amount());
}

TEST(DockMagnifierTest, APointerElsewhereGlidesTheFocusOver)
{
    DockMagnifier magnifier;
    magnifier.setEnabled(true);
    magnifier.track(100);
    double now = 1000;
    settle(magnifier, now);
    magnifier.leave();
    magnifier.advance(now += kFrameMs);
    ASSERT_GT(magnifier.amount(), 0);

    ASSERT_TRUE(magnifier.track(300));
    EXPECT_EQ(100, magnifier.focus());
    double previous = 100;
    double reached = -1;
    const double start = now;
    while (magnifier.isMoving()) {
        magnifier.advance(now += kFrameMs);
        EXPECT_GE(magnifier.focus(), previous);
        EXPECT_LE(magnifier.focus(), 300);
        if (reached < 0 && magnifier.focus() >= 298)
            reached = now - start;
        previous = magnifier.focus();
    }
    EXPECT_EQ(300, magnifier.focus());
    EXPECT_EQ(1, magnifier.amount());
    EXPECT_LE(reached, DockMagnifier::kFocusSettleMs + kFrameMs);
    magnifier.track(320);
    EXPECT_EQ(320, magnifier.focus());

    magnifier.leave();
    settle(magnifier, now);
    magnifier.track(40);
    EXPECT_EQ(40, magnifier.focus());
}

TEST(DockMagnifierTest, AHoldKeepsItAsItIsUntilReleased)
{
    DockMagnifier magnifier;
    magnifier.setEnabled(true);
    magnifier.track(100);
    double now = 1000;
    magnifier.advance(now += kFrameMs);
    magnifier.setHeld(true);
    EXPECT_FALSE(magnifier.track(400));
    EXPECT_FALSE(magnifier.leave());
    settle(magnifier, now);
    EXPECT_EQ(1, magnifier.amount());
    EXPECT_EQ(100, magnifier.focus());
    for (int i = 0; i < 30; ++i)
        magnifier.advance(now += kFrameMs);
    EXPECT_EQ(1, magnifier.amount());
    EXPECT_EQ(100, magnifier.focus());

    magnifier.setHeld(false);
    ASSERT_TRUE(magnifier.track(160));
    EXPECT_EQ(100, magnifier.focus());
    settle(magnifier, now);
    EXPECT_EQ(160, magnifier.focus());
    EXPECT_EQ(1, magnifier.amount());

    magnifier.setHeld(true);
    magnifier.setHeld(false);
    magnifier.leave();
    EXPECT_TRUE(magnifier.isMoving());
    settle(magnifier, now);
    EXPECT_EQ(0, magnifier.amount());
    EXPECT_EQ(160, magnifier.focus());
}

TEST(DockMagnifierTest, ASuppressionFadesItOutUntilItEnds)
{
    DockMagnifier magnifier;
    magnifier.setEnabled(true);
    magnifier.track(100);
    double now = 1000;
    settle(magnifier, now);
    magnifier.setSuppressed(true);
    EXPECT_FALSE(magnifier.isTracking());
    EXPECT_FALSE(magnifier.track(200));
    settle(magnifier, now);
    EXPECT_EQ(0, magnifier.amount());
    EXPECT_EQ(100, magnifier.focus());

    magnifier.setSuppressed(false);
    ASSERT_TRUE(magnifier.track(200));
    EXPECT_EQ(200, magnifier.focus());
    settle(magnifier, now);
    EXPECT_EQ(1, magnifier.amount());
}

TEST(DockMagnifierTest, TheFirstFrameAfterRestingIsOneIntervalOn)
{
    DockMagnifier a;
    DockMagnifier b;
    for (DockMagnifier *magnifier : {&a, &b}) {
        magnifier->setEnabled(true);
        magnifier->track(100);
    }
    a.advance(5000);
    b.stop();
    b.advance(123456);
    EXPECT_EQ(a.amount(), b.amount());
    EXPECT_GT(a.amount(), 0);
}

TEST(DockMagnifierTest, AFrameStampedLateMovesNoMoreThanAFrame)
{
    DockMagnifier even;
    DockMagnifier late;
    DockMagnifier fast;
    for (DockMagnifier *magnifier : {&even, &late, &fast}) {
        magnifier->setEnabled(true);
        magnifier->track(100);
        magnifier->advance(1000);
    }
    even.advance(1000 + kFrameMs);
    late.advance(1000 + 1.7 * kFrameMs);
    EXPECT_NEAR(even.amount(), late.amount(), 1e-12);
    fast.advance(1000 + kFrameMs / 2);
    EXPECT_LT(fast.amount(), even.amount());
    EXPECT_GT(fast.amount(), 0);
}
