// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "accelerators/accelerator.h"
#include "accelerators/hot_corner_controller.h"
#include "accelerators/hot_corner_settings.h"
#include "base/geometry.h"
#include "base/settings.h"
#include "input_test_util.h"

using namespace cutefish::accelerators;
namespace base = cutefish::base;

namespace {

const base::Rect kLeft(0, 0, 1000, 800);
const base::Rect kRight(1000, 0, 1000, 800);

class HotCornerTest : public testing::Test
{
protected:
    std::vector<AcceleratorAction> performed;
    HotCornerController controller {nullptr, [this](AcceleratorAction action) {
                                        performed.push_back(action);
                                        return true;
                                    }};

    void SetUp() override
    {
        controller.onPointerMoved(base::PointF(500, 400));
        controller.setOutputs({kLeft});
        controller.setBinding(HotCorner::TopLeft, {AcceleratorAction::ToggleOverview, NoModifier});
        controller.setBinding(HotCorner::BottomRight,
                              {AcceleratorAction::ToggleShowDesktop, NoModifier});
    }
};

TEST_F(HotCornerTest, OneOutputHasFourCorners)
{
    const auto corners = controller.corners();
    ASSERT_EQ(corners.size(), 4u);
    EXPECT_EQ(corners[0].second, base::Point(0, 0));
    EXPECT_EQ(corners[1].second, base::Point(999, 0));
    EXPECT_EQ(corners[2].second, base::Point(0, 799));
    EXPECT_EQ(corners[3].second, base::Point(999, 799));
}

TEST_F(HotCornerTest, CornersWhereOutputsMeetDoNotCount)
{
    controller.setOutputs({kLeft, kRight});
    const auto corners = controller.corners();
    ASSERT_EQ(corners.size(), 4u);
    EXPECT_EQ(corners[0], std::make_pair(HotCorner::TopLeft, base::Point(0, 0)));
    EXPECT_EQ(corners[1], std::make_pair(HotCorner::BottomLeft, base::Point(0, 799)));
    EXPECT_EQ(corners[2], std::make_pair(HotCorner::TopRight, base::Point(1999, 0)));
    EXPECT_EQ(corners[3], std::make_pair(HotCorner::BottomRight, base::Point(1999, 799)));
}

TEST_F(HotCornerTest, ATallerOutputKeepsTheCornerBesideTheShorterOne)
{
    const base::Rect tall(1000, 0, 1000, 1000);
    controller.setOutputs({kLeft, tall});
    bool found = false;
    for (const auto &[corner, point] : controller.corners()) {
        if (corner == HotCorner::BottomLeft && point == base::Point(1000, 999))
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(HotCornerTest, FiresOnArrival)
{
    controller.onPointerMoved(base::PointF(10, 10));
    EXPECT_TRUE(performed.empty());
    controller.onPointerMoved(base::PointF(0, 0));
    EXPECT_EQ(performed, std::vector {AcceleratorAction::ToggleOverview});
    controller.onPointerMoved(base::PointF(999.5, 799.5));
    EXPECT_EQ(performed.back(), AcceleratorAction::ToggleShowDesktop);
}

TEST_F(HotCornerTest, FiresOncePerVisit)
{
    controller.onPointerMoved(base::PointF(0, 0));
    controller.onPointerMoved(base::PointF(3, 5));
    controller.onPointerMoved(base::PointF(0, 0));
    EXPECT_EQ(performed.size(), 1u);
    controller.onPointerMoved(base::PointF(HotCornerController::kRearmDistance + 1, 0));
    controller.onPointerMoved(base::PointF(0, 0));
    EXPECT_EQ(performed.size(), 2u);
}

TEST_F(HotCornerTest, ADragUsesTheVisitUp)
{
    controller.onPointerMoved(base::PointF(0, 0), true);
    EXPECT_TRUE(performed.empty());
    controller.onPointerMoved(base::PointF(0, 0));
    EXPECT_TRUE(performed.empty());
    controller.onPointerMoved(base::PointF(HotCornerController::kRearmDistance + 1, 0));
    controller.onPointerMoved(base::PointF(0, 0));
    EXPECT_EQ(performed.size(), 1u);
}

TEST_F(HotCornerTest, ACornerWithoutAnActionDoesNothing)
{
    controller.onPointerMoved(base::PointF(999, 0));
    controller.onPointerMoved(base::PointF(0, 799));
    EXPECT_TRUE(performed.empty());
}

TEST_F(HotCornerTest, ARequiredModifierMustBeHeld)
{
    controller.setBinding(HotCorner::TopLeft, {AcceleratorAction::ToggleOverview, AltDown});
    controller.onPointerMoved(base::PointF(0, 0));
    EXPECT_TRUE(performed.empty());
}

TEST_F(HotCornerTest, TheCornerUnderThePointerWaitsForItToLeave)
{
    controller.onPointerMoved(base::PointF(0, 0));
    performed.clear();
    controller.setOutputs({kLeft});
    controller.onPointerMoved(base::PointF(0, 0));
    EXPECT_TRUE(performed.empty());
}

TEST(HotCornerSettingsTest, KeepsCornersInTheSettingsFile)
{
    input_test::TemporaryDirectory dir;
    ASSERT_TRUE(dir.isValid());
    const std::string file = dir.filePath("compositor.conf");
    {
        HotCornerSettings settings(file);
        EXPECT_FALSE(settings.binding(HotCorner::TopLeft).action);
        settings.setCorner("top-left", "toggle-overview", 0);
        settings.setCorner("bottom-right", "lock-screen",
                           int(ControlDown | AltGrDown));
        settings.setCorner("nowhere", "lock-screen", 0);
        EXPECT_EQ(settings.topLeft(), "toggle-overview");
        EXPECT_EQ(settings.bottomRightModifiers(), int(ControlDown));
    }
    HotCornerSettings reloaded(file);
    EXPECT_EQ(reloaded.binding(HotCorner::TopLeft),
              (HotCornerBinding {AcceleratorAction::ToggleOverview, NoModifier}));
    EXPECT_EQ(reloaded.binding(HotCorner::BottomRight),
              (HotCornerBinding {AcceleratorAction::LockScreen, ControlDown}));

    reloaded.setCorner("top-left", "no-such-action", 0);
    EXPECT_TRUE(reloaded.topLeft().empty());
    cutefish::base::Settings raw(file);
    EXPECT_FALSE(raw.contains("HotCorners/TopLeft"));
    EXPECT_EQ(raw.stringValue("HotCorners/BottomRight"), "lock-screen");
}

} // namespace
