// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "base/geometry.h"
#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "fake_renderer.h"
#include "input/input_manager.h"
#include "input/pointer.h"
#include "input/pointer_constraint_delegate.h"
#include "input/pointer_delegate.h"
#include "input/seat.h"
#include "loop_test_util.h"
#include "scene/node.h"
#include "scene/scene.h"
#include "shm_test_util.h"
#include "wm/dock_layout_manager.h"
#include "wm/window.h"
#include "wm/window_manager.h"

using namespace cutefish;
using namespace cutefish::wm;

namespace {

input::Timestamp at(int ms)
{
    return input::Timestamp(std::chrono::milliseconds(ms));
}

const base::Rect kOutput(0, 0, 1000, 800);
const base::Rect kDockBounds(200, 720, 600, 75);

class TestPointerDelegate : public input::PointerDelegate
{
public:
    void onPointerDestroying(input::Pointer *) override {}
    bool canAcceptPointerEventsForSurface(compositor::Surface *) const override { return true; }
    void onPointerEnter(compositor::Surface *, const base::PointF &, input::ButtonFlags) override {}
    void onPointerLeave(compositor::Surface *) override {}
    void onPointerMotion(input::Timestamp, const base::PointF &) override {}
    void onPointerButton(input::Timestamp, input::ButtonFlags, bool) override {}
    void onPointerScroll(input::Timestamp, const base::PointF &, bool) override {}
    void onFingerScrollStop(input::Timestamp) override {}
    void onPointerFrame() override {}
};

class TestConstraint : public input::PointerConstraintDelegate
{
public:
    TestConstraint(input::PointerConstraints *constraints, compositor::Surface *surface, Kind kind)
        : m_constraints(constraints), m_surface(surface), m_kind(kind)
    {
        EXPECT_TRUE(m_constraints->add(this));
    }

    ~TestConstraint() override { m_constraints->remove(this); }

    Kind kind() const override { return m_kind; }
    void onConstraintActivated() override {}
    void onAlreadyConstrained() override {}
    void onConstraintBroken() override {}
    void onDefunct() override { m_surface = nullptr; }
    bool isPersistent() const override { return true; }
    compositor::Surface *constrainedSurface() const override { return m_surface; }
    std::optional<base::Region> region() const override { return std::nullopt; }
    std::optional<base::PointF> cursorPositionHint() const override { return std::nullopt; }

private:
    input::PointerConstraints *m_constraints;
    compositor::Surface *m_surface;
    Kind m_kind;
};

class DockLayoutTest : public testing::Test
{
protected:
    scene::Scene scene;
    scene::Node *surfaceContainer =
            scene.root()->addChild(std::make_unique<scene::Node>("surfaces"));
    compositor::Compositor compositor {surfaceContainer};
    testsupport::FakeRenderer renderer;
    input::Seat seat {&compositor};
    input::InputManager input {&seat};
    WindowManager manager {&compositor, &seat};
    DockLayoutManager layout {&manager};

    std::vector<std::unique_ptr<compositor::Surface>> surfaces;
    std::shared_ptr<compositor::ShmPool> pool =
            testsupport::makePool(kOutput.width() * kOutput.height() * 4);
    Window *dock = nullptr;

    void SetUp() override
    {
        scene.setSize(kOutput.size());
        manager.setOutputBounds(kOutput);
        input.setBounds(kOutput.size());
        input.setEventSink(&manager);
        dock = makeWindow(kDockBounds);
        dock->setRole(WindowRole::Dock);
        dock->setFocusPolicy(FocusPolicy::NoFocus);
        moveTo(base::PointF(500, 400));
        finishSlide();
    }

    void TearDown() override { seat.shutdown(); }

    compositor::Surface *makeSurface(const base::Size &size)
    {
        surfaces.push_back(compositor.createSurface());
        compositor::Surface *surface = surfaces.back().get();
        auto buffer = pool->createBuffer(size, 0, size.width() * 4, render::PixelFormat::Bgrx8888,
                                         nullptr);
        surface->attach(buffer, {});
        surface->damage(base::Rect(base::Point(), size));
        surface->commit();
        compositor.updateTextures(renderer);
        return surface;
    }

    Window *makeWindow(const base::Rect &bounds)
    {
        Window *window =
                manager.createWindow(makeSurface(bounds.size()), {.requestedSize = bounds.size()});
        window->setBounds(bounds);
        return window;
    }

    void moveTo(const base::PointF &location) { manager.onPointerMotion(at(0), location); }

    void waitForReveal()
    {
        test::runFor(std::chrono::milliseconds(DockLayoutManager::kAutoHideDelayMs + 200));
    }

    void finishSlide()
    {
        layout.advance(1000);
        layout.advance(1000 + DockLayoutManager::kSlideDurationMs);
    }

    bool isShown() const { return layout.autoHideState(dock) == DockAutoHideState::Shown; }
};

TEST_F(DockLayoutTest, NeverHidesStaysPutOverMaximizedWindows)
{
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));
    window->maximize();

    EXPECT_EQ(DockVisibilityState::Visible, layout.visibilityState(dock));
    EXPECT_TRUE(isShown());
    EXPECT_EQ(base::Point(), dock->presentationOffset());
    EXPECT_FALSE(layout.isSliding(dock));
}

TEST_F(DockLayoutTest, FullscreenWindowHidesTheDockWhateverTheSetting)
{
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));
    window->activate();
    window->setFullscreen(true);

    EXPECT_EQ(DockVisibilityState::AutoHide, layout.visibilityState(dock));
    EXPECT_FALSE(isShown());
    EXPECT_TRUE(layout.isSliding(dock));

    finishSlide();
    EXPECT_FALSE(layout.isSliding(dock));
    EXPECT_TRUE(dock->isPresentationHidden());
    EXPECT_EQ(kOutput.bottom(), dock->node()->absoluteGeometry().top());

    moveTo(base::PointF(500, 799));
    waitForReveal();
    EXPECT_TRUE(isShown());

    window->setFullscreen(false);
    EXPECT_TRUE(isShown());
    EXPECT_FALSE(dock->isPresentationHidden());
    finishSlide();
    EXPECT_EQ(base::Point(), dock->presentationOffset());
}

class FullscreenPointerConstraintDockTest
    : public DockLayoutTest,
      public testing::WithParamInterface<input::PointerConstraintDelegate::Kind>
{
};

TEST_P(FullscreenPointerConstraintDockTest, ConstraintCancelsEdgeRevealAndLaunchpadCanShowDock)
{
    TestPointerDelegate pointerDelegate;
    input::Pointer pointer(&pointerDelegate, &seat);
    Window *launchpad = makeWindow(base::Rect(0, 0, 300, 200));
    launchpad->setRole(WindowRole::Launchpad);
    launchpad->setVisible(false);
    Window *window = makeWindow(kOutput);
    window->activate();
    window->setFullscreen(true);
    finishSlide();

    input.onPointerMotionLogical(at(1), base::PointF(500, 799));
    ASSERT_EQ(window->rootSurface(), seat.pointerFocus());
    ASSERT_FALSE(isShown());
    {
        TestConstraint constraint(seat.pointerConstraints(), window->rootSurface(), GetParam());
        ASSERT_EQ(&constraint, seat.pointerConstraints()->active());
        waitForReveal();
        EXPECT_FALSE(isShown());
        EXPECT_TRUE(dock->isPresentationHidden());
        EXPECT_TRUE(dock->node()->isEventTargetingBlocked());

        launchpad->setVisible(true);
        ASSERT_EQ(&constraint, seat.pointerConstraints()->active());
        waitForReveal();
        EXPECT_TRUE(isShown());

        launchpad->setVisible(false);
        EXPECT_FALSE(isShown());
    }
    EXPECT_FALSE(isShown());
    waitForReveal();
    EXPECT_TRUE(isShown());
}

INSTANTIATE_TEST_SUITE_P(PointerConstraints, FullscreenPointerConstraintDockTest,
        testing::Values(input::PointerConstraintDelegate::Kind::Confine,
                        input::PointerConstraintDelegate::Kind::Lock));

TEST_F(DockLayoutTest, PanelOnlyGivesWayToFullscreen)
{
    Window *panel = makeWindow(base::Rect(0, 0, 1000, 30));
    panel->setRole(WindowRole::Panel);
    layout.setBehavior(DockAutoHideBehavior::Always);
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));
    window->activate();
    EXPECT_EQ(DockVisibilityState::Visible, layout.visibilityState(panel));
    EXPECT_FALSE(isShown());

    window->maximize();
    EXPECT_EQ(DockVisibilityState::Visible, layout.visibilityState(panel));

    window->setFullscreen(true);
    EXPECT_EQ(DockVisibilityState::AutoHide, layout.visibilityState(panel));
    EXPECT_EQ(DockAutoHideState::Hidden, layout.autoHideState(panel));
    finishSlide();
    EXPECT_TRUE(panel->isPresentationHidden());
    EXPECT_EQ(base::Point(0, -30), panel->presentationOffset());

    moveTo(base::PointF(500, 2));
    waitForReveal();
    EXPECT_EQ(DockAutoHideState::Hidden, layout.autoHideState(panel));
    window->rootSurface()->setFullscreenBars(0, false);
    layout.update();
    waitForReveal();
    EXPECT_EQ(DockAutoHideState::Shown, layout.autoHideState(panel));

    window->setFullscreen(false);
    EXPECT_EQ(DockVisibilityState::Visible, layout.visibilityState(panel));
}

TEST_F(DockLayoutTest, DocksAndPanelsSlideInFromTheirEdgeAsTheyMap)
{
    Window *panel = makeWindow(base::Rect(0, 0, 1000, 30));
    panel->setRole(WindowRole::Panel);
    layout.update();

    EXPECT_EQ(DockAutoHideState::Shown, layout.autoHideState(panel));
    EXPECT_TRUE(layout.isSliding(panel));
    EXPECT_FALSE(panel->isPresentationHidden());
    EXPECT_EQ(base::Point(0, -30), panel->presentationOffset());
    layout.advance(1000);
    layout.advance(1000 + DockLayoutManager::kSlideDurationMs / 2);
    EXPECT_LT(-30, panel->presentationOffset().y());
    EXPECT_GT(0, panel->presentationOffset().y());
    layout.advance(1000 + DockLayoutManager::kSlideDurationMs);
    EXPECT_FALSE(layout.isSliding(panel));
    EXPECT_EQ(base::Point(), panel->presentationOffset());
}

TEST_F(DockLayoutTest, DocksAndPanelsWaitForTheSessionToStart)
{
    layout.setSessionStarting(true);
    Window *panel = makeWindow(base::Rect(0, 0, 1000, 30));
    panel->setRole(WindowRole::Panel);
    layout.update();
    EXPECT_EQ(DockAutoHideState::Hidden, layout.autoHideState(panel));
    EXPECT_FALSE(layout.isSliding(panel));
    EXPECT_TRUE(panel->isPresentationHidden());
    EXPECT_EQ(base::Point(0, -30), panel->presentationOffset());

    layout.setSessionStarting(false);
    EXPECT_EQ(DockAutoHideState::Shown, layout.autoHideState(panel));
    EXPECT_TRUE(layout.isSliding(panel));
    EXPECT_FALSE(panel->isPresentationHidden());
    finishSlide();
    EXPECT_EQ(base::Point(), panel->presentationOffset());
}

TEST_F(DockLayoutTest, DockMappedOverAFullscreenWindowStaysParked)
{
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));
    window->activate();
    window->setFullscreen(true);
    Window *second = makeWindow(base::Rect(0, 720, 1000, 75));
    second->setRole(WindowRole::Dock);
    layout.update();

    EXPECT_EQ(DockAutoHideState::Hidden, layout.autoHideState(second));
    EXPECT_FALSE(layout.isSliding(second));
    EXPECT_TRUE(second->isPresentationHidden());
    EXPECT_EQ(base::Point(0, kOutput.bottom() - 720), second->presentationOffset());
}

TEST_F(DockLayoutTest, EndingTheSessionPutsDocksAndPanelsAwayUntilItCarriesOn)
{
    Window *panel = makeWindow(base::Rect(0, 0, 1000, 30));
    panel->setRole(WindowRole::Panel);
    moveTo(base::PointF(500, centerPixel(kDockBounds).y()));

    layout.setSessionEnding(true);
    EXPECT_FALSE(isShown());
    EXPECT_EQ(DockAutoHideState::Hidden, layout.autoHideState(panel));
    finishSlide();
    EXPECT_TRUE(dock->isPresentationHidden());
    EXPECT_EQ(base::Point(0, -30), panel->presentationOffset());

    layout.setSessionEnding(false);
    EXPECT_TRUE(isShown());
    EXPECT_EQ(DockAutoHideState::Shown, layout.autoHideState(panel));
    finishSlide();
    EXPECT_EQ(base::Point(), dock->presentationOffset());
    EXPECT_EQ(base::Point(), panel->presentationOffset());
}

TEST_F(DockLayoutTest, EndingTheSessionHidesTheDesktopUntilItCarriesOn)
{
    Window *desktop = makeWindow(kOutput);
    desktop->setRole(WindowRole::Desktop);
    layout.update();
    EXPECT_FALSE(desktop->isPresentationHidden());

    layout.setSessionEnding(true);
    EXPECT_TRUE(desktop->isPresentationHidden());
    EXPECT_FALSE(desktop->node()->isEffectivelyVisible());

    Window *second = makeWindow(kOutput);
    second->setRole(WindowRole::Desktop);
    layout.update();
    EXPECT_TRUE(second->isPresentationHidden());

    layout.setSessionEnding(false);
    EXPECT_FALSE(desktop->isPresentationHidden());
    EXPECT_FALSE(second->isPresentationHidden());
    EXPECT_EQ(base::Point(), desktop->presentationOffset());
}

TEST_F(DockLayoutTest, DialogOverFullscreenWindowKeepsTheDockAway)
{
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));
    window->setFullscreen(true);
    Window *dialog = manager.createWindow(makeSurface(base::Size(200, 100)),
                                          {.requestedSize = base::Size(200, 100),
                                           .transientParent = window});
    dialog->activate();

    EXPECT_EQ(WorkspaceState::Fullscreen, layout.workspaceStateFor(kOutput));
    EXPECT_FALSE(isShown());
}

TEST_F(DockLayoutTest, InactiveFullscreenWindowUnderAnotherDoesNotHide)
{
    Window *fullscreen = makeWindow(base::Rect(100, 100, 300, 200));
    fullscreen->setFullscreen(true);
    Window *front = makeWindow(base::Rect(100, 100, 300, 200));
    front->activate();

    EXPECT_EQ(WorkspaceState::Default, layout.workspaceStateFor(kOutput));
    EXPECT_TRUE(isShown());
}

TEST_F(DockLayoutTest, WhenObstructedFollowsMaximizedWindows)
{
    layout.setBehavior(DockAutoHideBehavior::WhenObstructed);
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));
    EXPECT_TRUE(isShown());

    window->maximize();
    EXPECT_EQ(DockVisibilityState::AutoHide, layout.visibilityState(dock));
    EXPECT_FALSE(isShown());

    window->restore();
    EXPECT_TRUE(isShown());

    window->maximize();
    EXPECT_FALSE(isShown());
    window->minimize();
    EXPECT_TRUE(isShown());
}

TEST_F(DockLayoutTest, YieldingZoneLeavesTheWorkAreaToMaximizedWindows)
{
    layout.setBehavior(DockAutoHideBehavior::WhenObstructed);
    manager.setExclusiveZone(dock, ScreenEdge::Bottom, 80, {}, true);
    const base::Rect reserved(0, 0, 1000, 720);
    EXPECT_EQ(reserved, manager.workAreaForOutput(kOutput));

    const base::Rect underTheDock(100, 600, 300, 190);
    Window *other = makeWindow(underTheDock);
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));

    window->maximize();
    EXPECT_EQ(kOutput, manager.workAreaForOutput(kOutput));
    EXPECT_EQ(kOutput, window->bounds());
    EXPECT_EQ(reserved, manager.restingWorkAreaForOutput(kOutput));
    EXPECT_EQ(reserved, manager.outputAreas().front().workArea);

    window->restore();
    EXPECT_EQ(reserved, manager.workAreaForOutput(kOutput));
    EXPECT_EQ(base::Rect(100, 100, 300, 200), window->bounds());
    EXPECT_EQ(underTheDock, other->bounds());

    window->maximize();
    window->minimize();
    EXPECT_EQ(reserved, manager.workAreaForOutput(kOutput));
}

TEST_F(DockLayoutTest, FixedZoneStaysUnderMaximizedWindows)
{
    manager.setExclusiveZone(dock, ScreenEdge::Bottom, 80);
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));

    window->maximize();
    EXPECT_EQ(base::Rect(0, 0, 1000, 720), manager.workAreaForOutput(kOutput));
    EXPECT_EQ(base::Rect(0, 0, 1000, 720), window->bounds());
}

TEST_F(DockLayoutTest, AlwaysHideStaysShownWithNothingToMakeRoomFor)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    EXPECT_EQ(DockVisibilityState::AutoHide, layout.visibilityState(dock));
    EXPECT_TRUE(isShown());

    Window *window = makeWindow(base::Rect(100, 100, 300, 200));
    EXPECT_FALSE(isShown());

    manager.destroyWindow(window);
    EXPECT_FALSE(isShown());
    waitForReveal();
    EXPECT_TRUE(isShown());
}

TEST_F(DockLayoutTest, PointerAtTheEdgeRevealsAfterTheDelay)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    finishSlide();
    ASSERT_FALSE(isShown());
    ASSERT_TRUE(dock->isPresentationHidden());

    moveTo(base::PointF(500, 799));
    EXPECT_FALSE(isShown());

    waitForReveal();
    EXPECT_TRUE(isShown());
    EXPECT_FALSE(dock->isPresentationHidden());
    EXPECT_TRUE(layout.isSliding(dock));
}

TEST_F(DockLayoutTest, LeavingTheEdgeBeforeTheDelayCancelsTheReveal)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    finishSlide();

    moveTo(base::PointF(500, 799));
    moveTo(base::PointF(500, 400));
    waitForReveal();
    EXPECT_FALSE(isShown());
}

TEST_F(DockLayoutTest, PointerMovingOntoTheDockDuringTheDelayStillReveals)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    finishSlide();

    moveTo(base::PointF(500, 799));
    moveTo(base::PointF(500, 750));
    waitForReveal();
    EXPECT_TRUE(isShown());
}

TEST_F(DockLayoutTest, LeavingTheDockHidesAfterTheDelayAndTheGapCounts)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    finishSlide();
    moveTo(base::PointF(500, 799));
    waitForReveal();
    ASSERT_TRUE(isShown());
    finishSlide();

    moveTo(base::PointF(500, 797));
    waitForReveal();
    EXPECT_TRUE(isShown());
    moveTo(base::PointF(50, 798));
    waitForReveal();
    EXPECT_TRUE(isShown());

    moveTo(base::PointF(500, 700));
    EXPECT_TRUE(isShown());
    waitForReveal();
    EXPECT_FALSE(isShown());
    EXPECT_TRUE(layout.isSliding(dock));
}

TEST_F(DockLayoutTest, ComingBackBeforeTheDelayKeepsTheDock)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    finishSlide();
    moveTo(base::PointF(500, 799));
    waitForReveal();
    finishSlide();
    moveTo(base::PointF(500, 750));
    ASSERT_TRUE(isShown());

    moveTo(base::PointF(500, 650));
    moveTo(base::PointF(500, 750));
    waitForReveal();
    EXPECT_TRUE(isShown());
    EXPECT_FALSE(layout.isSliding(dock));
}

TEST_F(DockLayoutTest, TooltipGoingWithThePointerWaitsForTheDelay)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    finishSlide();
    moveTo(base::PointF(500, 799));
    waitForReveal();
    finishSlide();
    moveTo(base::PointF(500, 750));
    Window *tooltip = manager.createWindow(makeSurface(base::Size(100, 40)),
                                           {.type = WindowType::Popup, .transientParent = dock});
    ASSERT_TRUE(isShown());

    moveTo(base::PointF(500, 650));
    manager.destroyWindow(tooltip);
    EXPECT_TRUE(isShown());
    waitForReveal();
    EXPECT_FALSE(isShown());
}

TEST_F(DockLayoutTest, AnythingButThePointerHidesAtOnce)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    EXPECT_TRUE(isShown());
    makeWindow(base::Rect(100, 100, 300, 200));
    EXPECT_FALSE(isShown());
}

TEST_F(DockLayoutTest, HiddenDockTakesNoInputWhileItSlidesAway)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    EXPECT_FALSE(dock->node()->isEventTargetingBlocked());
    makeWindow(base::Rect(100, 100, 300, 200));
    ASSERT_TRUE(layout.isSliding(dock));
    EXPECT_TRUE(dock->node()->isEventTargetingBlocked());
    finishSlide();

    moveTo(base::PointF(500, 799));
    waitForReveal();
    ASSERT_TRUE(layout.isSliding(dock));
    EXPECT_FALSE(dock->node()->isEventTargetingBlocked());
}

TEST_F(DockLayoutTest, OpenMenuAndLaunchpadKeepTheDockShown)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    ASSERT_FALSE(isShown());

    Window *menu = manager.createWindow(makeSurface(base::Size(100, 40)),
                                        {.type = WindowType::Popup, .transientParent = dock});
    waitForReveal();
    EXPECT_TRUE(isShown());
    manager.destroyWindow(menu);
    EXPECT_FALSE(isShown());

    Window *launchpad = makeWindow(kOutput);
    launchpad->setRole(WindowRole::Launchpad);
    layout.update();
    waitForReveal();
    EXPECT_TRUE(isShown());
    manager.destroyWindow(launchpad);
    EXPECT_FALSE(isShown());
}

TEST_F(DockLayoutTest, SlideEasesToTheTargetAndParksOffScreen)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    ASSERT_TRUE(layout.isSliding(dock));

    layout.advance(1000);
    EXPECT_EQ(base::Point(), dock->presentationOffset());
    layout.advance(1000 + DockLayoutManager::kSlideDurationMs / 2);
    const int half = dock->presentationOffset().y();
    EXPECT_GT(half, 0);
    EXPECT_LT(half, kOutput.bottom() - kDockBounds.top());
    EXPECT_FALSE(dock->isPresentationHidden());

    layout.advance(1000 + DockLayoutManager::kSlideDurationMs);
    EXPECT_EQ(base::Point(0, kOutput.bottom() - kDockBounds.top()), dock->presentationOffset());
    EXPECT_TRUE(dock->isPresentationHidden());
    EXPECT_FALSE(layout.isSliding(dock));
}

TEST_F(DockLayoutTest, ReversingMidSlideStartsFromWhereTheDockIs)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));
    layout.advance(1000);
    layout.advance(1000 + DockLayoutManager::kSlideDurationMs / 2);
    const base::Point midway = dock->presentationOffset();

    manager.destroyWindow(window);
    waitForReveal();
    ASSERT_TRUE(isShown());
    layout.advance(2000);
    EXPECT_EQ(midway, dock->presentationOffset());
    layout.advance(2000 + DockLayoutManager::kSlideDurationMs);
    EXPECT_EQ(base::Point(), dock->presentationOffset());
}

TEST_F(DockLayoutTest, HiddenDockFollowsItsOwnBoundsOffScreen)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    finishSlide();

    dock->setBounds(base::Rect(200, 700, 600, 95));
    finishSlide();
    EXPECT_EQ(base::Point(0, kOutput.bottom() - 700), dock->presentationOffset());
    EXPECT_TRUE(dock->isPresentationHidden());
}

TEST_F(DockLayoutTest, ADockDrawingPastItsReachHidesAndStaysShownAsFarAsItReaches)
{
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    finishSlide();

    dock->setEdgeReach(kDockBounds.height());
    dock->setBounds(base::Rect(200, 680, 600, 115));
    finishSlide();
    EXPECT_EQ(base::Point(0, kOutput.bottom() - kDockBounds.y()), dock->presentationOffset());

    moveTo(base::PointF(500, 799));
    waitForReveal();
    ASSERT_TRUE(isShown());
    finishSlide();
    EXPECT_EQ(base::Point(), dock->presentationOffset());
    moveTo(base::PointF(500, 730));
    waitForReveal();
    EXPECT_TRUE(isShown());
    moveTo(base::PointF(500, 700));
    waitForReveal();
    EXPECT_FALSE(isShown());
}

TEST_F(DockLayoutTest, SideDockLeavesThroughItsOwnEdge)
{
    dock->setBounds(base::Rect(5, 100, 75, 600));
    layout.setBehavior(DockAutoHideBehavior::Always);
    makeWindow(base::Rect(100, 100, 300, 200));
    finishSlide();
    EXPECT_EQ(base::Point(-80, 0), dock->presentationOffset());

    moveTo(base::PointF(2, 400));
    waitForReveal();
    EXPECT_TRUE(isShown());
}

TEST_F(DockLayoutTest, TopPanelTellsAFullscreenWindowAndPushesItWhenAsked)
{
    Window *panel = makeWindow(base::Rect(0, 0, 1000, 30));
    panel->setRole(WindowRole::Panel);
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));
    window->activate();
    std::vector<std::pair<int, int>> reveals;
    base::Subscription watch = manager.fullscreenRevealChanged.connect(
            [&](Window *revealed, int offset, int duration) {
        if (revealed == window)
            reveals.emplace_back(offset, duration);
    });
    window->setFullscreen(true);
    finishSlide();
    EXPECT_TRUE(reveals.empty());

    window->rootSurface()->setFullscreenBars(40, true);
    moveTo(base::PointF(500, 2));
    waitForReveal();
    ASSERT_EQ(DockAutoHideState::Shown, layout.autoHideState(panel));
    ASSERT_EQ(1u, reveals.size());
    EXPECT_EQ(std::make_pair(30, DockLayoutManager::kSlideDurationMs), reveals.back());
    layout.advance(2000);
    EXPECT_EQ(base::Point(0, 0), window->presentationOffset());
    layout.advance(2000 + DockLayoutManager::kSlideDurationMs);
    EXPECT_EQ(base::Point(0, 30), window->presentationOffset());

    moveTo(base::PointF(500, 65));
    waitForReveal();
    EXPECT_EQ(DockAutoHideState::Shown, layout.autoHideState(panel));
    moveTo(base::PointF(500, 75));
    waitForReveal();
    EXPECT_EQ(DockAutoHideState::Hidden, layout.autoHideState(panel));
    EXPECT_EQ(std::make_pair(0, DockLayoutManager::kSlideDurationMs), reveals.back());
    finishSlide();
    EXPECT_EQ(base::Point(), window->presentationOffset());

    window->rootSurface()->setFullscreenBars(40, false);
    moveTo(base::PointF(500, 2));
    waitForReveal();
    finishSlide();
    EXPECT_EQ(DockAutoHideState::Shown, layout.autoHideState(panel));
    EXPECT_EQ(base::Point(), window->presentationOffset());
    EXPECT_EQ(30, reveals.back().first);

    window->setFullscreen(false);
    EXPECT_EQ(0, reveals.back().first);
}

TEST_F(DockLayoutTest, OnlyTheActiveFullscreenWindowIsPushedAndOnlyByTheBar)
{
    Window *panel = makeWindow(base::Rect(0, 0, 1000, 30));
    panel->setRole(WindowRole::Panel);
    Window *banner = makeWindow(base::Rect(600, 0, 400, 88));
    banner->setRole(WindowRole::Panel);
    Window *window = makeWindow(base::Rect(100, 100, 300, 200));
    window->activate();
    std::vector<int> reveals;
    base::Subscription watch = manager.fullscreenRevealChanged.connect(
            [&](Window *revealed, int offset, int) {
        if (revealed == window)
            reveals.push_back(offset);
    });
    window->setFullscreen(true);
    window->rootSurface()->setFullscreenBars(40, true);
    finishSlide();
    moveTo(base::PointF(500, 2));
    waitForReveal();
    finishSlide();
    ASSERT_EQ(DockAutoHideState::Shown, layout.autoHideState(panel));
    EXPECT_EQ(std::vector<int>({30}), reveals);
    EXPECT_EQ(base::Point(0, 30), window->presentationOffset());

    Window *other = makeWindow(base::Rect(200, 200, 300, 200));
    other->activate();
    ASSERT_TRUE(other->isActive());
    EXPECT_EQ(std::vector<int>({30, 0}), reveals);
    EXPECT_EQ(base::Point(), window->presentationOffset());
    moveTo(base::PointF(500, 300));
    waitForReveal();
    moveTo(base::PointF(500, 2));
    waitForReveal();
    finishSlide();
    EXPECT_EQ(std::vector<int>({30, 0}), reveals);
    EXPECT_EQ(base::Point(), window->presentationOffset());

    window->activate();
    moveTo(base::PointF(500, 300));
    waitForReveal();
    finishSlide();
    moveTo(base::PointF(500, 2));
    waitForReveal();
    finishSlide();
    EXPECT_EQ(30, reveals.back());
    EXPECT_EQ(base::Point(0, 30), window->presentationOffset());
}

} // namespace
