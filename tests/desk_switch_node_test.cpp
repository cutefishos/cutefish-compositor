// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include <gtest/gtest.h>

#include "fake_renderer.h"
#include "scene/desk_switch_node.h"
#include "scene/motion.h"
#include "scene/scene.h"
#include "scene/slide_out_node.h"
#include "base/geometry.h"

using namespace cutefish;
using Node = scene::DeskSwitchNode;

namespace {

class FrameTexture : public render::Texture
{
public:
    explicit FrameTexture(base::Size size)
        : m_size(size)
    {
    }

    base::Size size() const override { return m_size; }

private:
    base::Size m_size;
};

class FrameRenderer : public testsupport::FakeRenderer
{
public:
    int captures = 0;

    bool renderToTexture(std::unique_ptr<render::Texture> &texture, const base::Rect &bounds,
                         const std::function<void()> &paint) override
    {
        ++captures;
        paint();
        texture = std::make_unique<FrameTexture>(bounds.size());
        lastCapture = texture.get();
        return true;
    }

    void drawTextureF(render::Texture *texture, const base::RectF &target, float alpha) override
    {
        FakeRenderer::drawTextureF(texture, target, alpha);
        calls.back().target = target;
    }

    render::Texture *lastCapture = nullptr;
};

const base::Rect kViewport(0, 0, 1000, 800);
constexpr int kSettleMs = 1000;

struct Fixture {
    int64_t now = 1000;
    scene::Scene scene;
    scene::Node *container =
            scene.root()->addChild(std::make_unique<scene::Node>("surfaces"));
    scene::ColorNode *below = static_cast<scene::ColorNode *>(
            container->addChild(std::make_unique<scene::ColorNode>(render::Color {0, 1, 0, 1})));
    Node *node = static_cast<Node *>(container->addChild(std::make_unique<Node>([this] {
        return now;
    })));
    scene::ColorNode *above = static_cast<scene::ColorNode *>(
            container->addChild(std::make_unique<scene::ColorNode>(render::Color {1, 0, 0, 1})));
    FrameRenderer renderer;
    int endingTaken = 0;
    int finished = 0;

    Fixture()
    {
        scene.setSize(kViewport.size());
        below->setGeometry(base::Rect(10, 10, 100, 100));
        above->setGeometry(base::Rect(20, 20, 100, 100));
        node->setGeometry(kViewport);
        node->setEndingScreenshotCallback([this] {
            ++endingTaken;
        });
        node->setFinishedCallback([this] {
            ++finished;
        });
    }

    Node::Frames frames(render::Texture **texture, const std::string &output = "out")
    {
        Node::Frames result;
        auto frame = std::make_unique<FrameTexture>(kViewport.size());
        *texture = frame.get();
        result[output] = std::move(frame);
        return result;
    }

    std::vector<testsupport::FakeRenderer::Call> paint(const std::string &output = "out",
                                                 double scale = 1)
    {
        renderer.calls.clear();
        renderer.setOutputContext({output, kViewport, base::Size(1000, 800), scale, 0, 0, {}});
        node->render(renderer);
        return renderer.calls;
    }

    static std::optional<testsupport::FakeRenderer::Call>
    drawn(const std::vector<testsupport::FakeRenderer::Call> &calls, render::Texture *texture)
    {
        for (const auto &call : calls) {
            if (call.kind == "texture" && call.texture == texture)
                return call;
        }
        return std::nullopt;
    }
};

} // namespace

TEST(DeskSwitchNode, TheStartingFrameCoversTheDeskContent)
{
    Fixture f;
    EXPECT_FALSE(f.node->isActive());
    EXPECT_TRUE(f.paint().empty());

    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 3, 0, 1, f.frames(&starting), 1000);
    EXPECT_TRUE(f.node->isActive());
    EXPECT_TRUE(f.node->paintsSubtree());
    EXPECT_EQ(f.node->content(), scene::Node::Content::Opaque);

    const auto calls = f.paint();
    ASSERT_EQ(calls.size(), 1);
    EXPECT_EQ(calls[0].texture, starting);
    EXPECT_EQ(calls[0].rect, kViewport);
    EXPECT_TRUE(starting->contentOpaque);

    EXPECT_TRUE(f.paint("other").empty());
}

TEST(DeskSwitchNode, TheCoveredContentIsWhatLiesBelowTheNode)
{
    Fixture f;
    f.node->paintCoveredContent(f.renderer);
    ASSERT_EQ(f.renderer.calls.size(), 2);
    EXPECT_EQ(f.renderer.calls[0].kind, "clear");
    EXPECT_EQ(f.renderer.calls[1].kind, "fill");
    EXPECT_EQ(f.renderer.calls[1].rect, base::Rect(10, 10, 100, 100));
}

TEST(DeskSwitchNode, EveryOutputCopiesTheEndingDeskAtItsNextFrame)
{
    Fixture f;
    Node::Frames frames;
    frames["a"] = std::make_unique<FrameTexture>(kViewport.size());
    frames["b"] = std::make_unique<FrameTexture>(kViewport.size());
    f.node->start(Node::Type::Continuous, 2, 0, 1, std::move(frames), 1000);
    f.node->takeEndingScreenshot(1);
    EXPECT_FALSE(f.node->endingScreenshotTaken());

    f.renderer.capturing = true;
    f.paint("a");
    EXPECT_EQ(f.renderer.captures, 0);
    f.renderer.capturing = false;

    f.paint("a");
    EXPECT_EQ(f.renderer.captures, 1);
    EXPECT_EQ(f.endingTaken, 0);
    f.paint("a");
    EXPECT_EQ(f.renderer.captures, 1);
    f.paint("b");
    EXPECT_EQ(f.renderer.captures, 2);
    EXPECT_EQ(f.endingTaken, 1);
    EXPECT_TRUE(f.node->endingScreenshotTaken());
    EXPECT_FALSE(f.node->isAnimating());
}

TEST(DeskSwitchNode, AContinuousSwitchSlidesAWidthAndTheGapBetweenDesks)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    render::Texture *ending = f.renderer.lastCapture;

    f.node->startAnimation();
    auto calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, starting)->rect, kViewport);
    EXPECT_EQ(Fixture::drawn(calls, ending), std::nullopt);

    f.now += 150;
    calls = f.paint();
    const int halfway = Fixture::drawn(calls, starting)->rect.x();
    EXPECT_LT(halfway, 0);
    EXPECT_GT(halfway, -1050);
    EXPECT_EQ(Fixture::drawn(calls, ending)->rect.x(), halfway + 1000 + Node::kDesksSpacing);
    EXPECT_EQ(f.finished, 0);

    f.now += kSettleMs;
    calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, ending)->rect, kViewport);
    EXPECT_EQ(Fixture::drawn(calls, starting), std::nullopt);
    EXPECT_EQ(f.finished, 1);
    EXPECT_FALSE(f.node->isAnimating());
    f.paint();
    EXPECT_EQ(f.finished, 1);
}

TEST(DeskSwitchNode, AQuickSwitchMovesAQuarterAndFadesTheEndingDeskIn)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Quick, 2, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    render::Texture *ending = f.renderer.lastCapture;

    auto calls = f.paint();
    EXPECT_FLOAT_EQ(Fixture::drawn(calls, ending)->alpha, 0);
    f.node->startAnimation();
    calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, ending)->rect.x(), 250);
    EXPECT_FLOAT_EQ(Fixture::drawn(calls, ending)->alpha, 0);
    EXPECT_EQ(calls.back().texture, ending);

    f.now += Node::kQuickFadeInAnimationDurationMs;
    calls = f.paint();
    EXPECT_FLOAT_EQ(Fixture::drawn(calls, ending)->alpha, 1);

    f.now += Node::kQuickAnimationDurationMs;
    calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, ending)->rect, kViewport);
    EXPECT_EQ(f.finished, 1);
}

TEST(DeskSwitchNode, AGlideCrossesTheWholeGapOpaqueAndLandsOnTime)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Glide, 2, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    render::Texture *ending = f.renderer.lastCapture;

    f.node->startAnimation();
    auto calls = f.paint();
    ASSERT_TRUE(Fixture::drawn(calls, starting));
    EXPECT_EQ(Fixture::drawn(calls, starting)->rect, kViewport);
    EXPECT_FALSE(Fixture::drawn(calls, ending));

    f.now += Node::kGlideDurationMs / 2;
    calls = f.paint();
    const double eased = scene::motion::cubicBezier(0.2, 0, 0.4, 1, 0.5);
    ASSERT_TRUE(Fixture::drawn(calls, ending));
    EXPECT_NEAR(Fixture::drawn(calls, ending)->rect.x(),
                (1000 + Node::kDesksSpacing) * (1 - eased), 1);
    EXPECT_FLOAT_EQ(Fixture::drawn(calls, ending)->alpha, 1);
    EXPECT_EQ(f.finished, 0);

    f.now += Node::kGlideDurationMs / 2;
    calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, ending)->rect, kViewport);
    EXPECT_EQ(f.finished, 1);
}

TEST(DeskSwitchNode, ADeskOnTheLeftGoesInFrontWithoutMovingWhatIsShown)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 1, 0, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(0);
    auto calls = f.paint();
    render::Texture *ending = f.renderer.lastCapture;
    EXPECT_EQ(Fixture::drawn(calls, starting)->rect, kViewport);

    f.node->startAnimation();
    calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, starting)->rect, kViewport);
    EXPECT_EQ(Fixture::drawn(calls, ending), std::nullopt);
    f.now += 150;
    calls = f.paint();
    const int halfway = Fixture::drawn(calls, starting)->rect.x();
    EXPECT_GT(halfway, 0);
    EXPECT_EQ(Fixture::drawn(calls, ending)->rect.x(), halfway - 1050);
    f.now += kSettleMs;
    calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, ending)->rect, kViewport);
    EXPECT_EQ(Fixture::drawn(calls, starting), std::nullopt);
}

TEST(DeskSwitchNode, ReplacingNeedsAFrameOnlyForADeskWithout)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 3, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    f.node->startAnimation();

    EXPECT_TRUE(f.node->replaceAnimation(2));
    f.node->takeEndingScreenshot(2);
    f.paint();
    f.node->startAnimation();
    EXPECT_EQ(f.node->endingDesk(), 2);
    EXPECT_FALSE(f.node->replaceAnimation(1));
    f.node->startAnimation();
    f.now += kSettleMs;
    f.paint();
    EXPECT_EQ(f.node->mostVisibleDesk(), 1);
}

TEST(DeskSwitchNode, AKeyPressedMidSwitchBendsTowardsTheNextDeskWithoutStopping)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 3, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    f.node->startAnimation();
    f.now += 150;
    const int before = Fixture::drawn(f.paint(), starting)->rect.x();

    EXPECT_TRUE(f.node->replaceAnimation(2));
    f.node->takeEndingScreenshot(2);
    f.paint();
    f.node->startAnimation();
    f.now += 16;
    EXPECT_LT(Fixture::drawn(f.paint(), starting)->rect.x(), before - 30);
    f.now += kSettleMs;
    f.paint();
    EXPECT_EQ(f.node->mostVisibleDesk(), 2);
    EXPECT_EQ(f.finished, 1);
}

TEST(DeskSwitchNode, ASwipeMovesTheStripWithTheFingersFromTheFirstEvent)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 3, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    EXPECT_EQ(f.node->updateSwipeAnimation(-0.1, 1000), std::nullopt);
    EXPECT_TRUE(f.node->isTracking());
    auto calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, starting)->rect.x(), -105);

    EXPECT_EQ(f.node->updateSwipeAnimation(-0.4, 1008), std::nullopt);
    f.now = 1008 + Node::kResampleLatencyMs;
    calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, starting)->rect.x(), -525);
    EXPECT_EQ(Fixture::drawn(calls, f.renderer.lastCapture)->rect.x(), 525);
    EXPECT_EQ(f.node->mostVisibleDesk(), 1);
    EXPECT_EQ(f.finished, 0);
}

TEST(DeskSwitchNode, FramesShowTheFingersResampledAtTheirTime)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.node->updateSwipeAnimation(-0.1, 1000);
    f.node->updateSwipeAnimation(-0.002, 1008);

    f.now = 1004 + Node::kResampleLatencyMs;
    EXPECT_EQ(Fixture::drawn(f.paint(), starting)->rect.x(), -106);
    f.now = 1100;
    EXPECT_EQ(Fixture::drawn(f.paint(), starting)->rect.x(), -108);
}

TEST(DeskSwitchNode, FingersThatStopReportingAtSpeedHaveLiftedAndTheStripGoesOn)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    for (int i = 1; i <= 4; ++i)
        f.node->updateSwipeAnimation(-0.05, 1000 + i * 8);
    f.now = 1032 + Node::kResampleLatencyMs;
    int previous = Fixture::drawn(f.paint(), starting)->rect.x();
    for (int frame = 0; frame < 4; ++frame) {
        f.now += 16;
        const int x = Fixture::drawn(f.paint(), starting)->rect.x();
        EXPECT_LT(x, previous - 20) << frame;
        previous = x;
    }
    const double before = Fixture::drawn(f.paint(), starting)->target.x();
    EXPECT_EQ(f.node->endSwipeAnimation(f.now), 1);
    EXPECT_DOUBLE_EQ(Fixture::drawn(f.paint(), starting)->target.x(), before);
    f.now += kSettleMs;
    f.paint();
    EXPECT_EQ(f.finished, 1);
    EXPECT_EQ(f.node->mostVisibleDesk(), 1);
}

TEST(DeskSwitchNode, FingersThatSlowDownAndHoldKeepTheStrip)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    for (int i = 1; i <= 4; ++i)
        f.node->updateSwipeAnimation(-0.05, 1000 + i * 8);
    for (int i = 1; i <= 4; ++i)
        f.node->updateSwipeAnimation(-0.001, 1032 + i * 8);
    f.now = 1064 + Node::kResampleLatencyMs + Node::kResampleMaxPredictionMs;
    const int held = Fixture::drawn(f.paint(), starting)->rect.x();
    f.now += 200;
    EXPECT_EQ(Fixture::drawn(f.paint(), starting)->rect.x(), held);

    f.node->updateSwipeAnimation(-0.001, f.now - Node::kResampleLatencyMs);
    EXPECT_NEAR(Fixture::drawn(f.paint(), starting)->rect.x(), held - 1, 1);
}

TEST(DeskSwitchNode, PastAnEndTheStripGivesWayLessAndLess)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 1, 0, 0, f.frames(&starting), 1000);
    f.node->updateSwipeAnimation(0.05, 1000);
    f.now = 1000 + Node::kResampleLatencyMs;
    const int little = Fixture::drawn(f.paint(), starting)->rect.x();
    EXPECT_GT(little, 0);
    EXPECT_LT(little, 52);
    EXPECT_TRUE(f.node->reachedEdge());

    f.node->updateSwipeAnimation(3, 1008);
    f.now = 1008 + Node::kResampleLatencyMs;
    const int far = Fixture::drawn(f.paint(), starting)->rect.x();
    EXPECT_GT(far, little);
    EXPECT_LT(far, 100);
    EXPECT_EQ(f.node->mostVisibleDesk(), 0);

    f.now += 200;
    EXPECT_EQ(f.node->endSwipeAnimation(f.now), 0);
    f.now += kSettleMs;
    EXPECT_EQ(Fixture::drawn(f.paint(), starting)->rect, kViewport);
    EXPECT_EQ(f.finished, 1);
}

TEST(DeskSwitchNode, AFlungSwipeGoesOnToTheNextDeskAndAHeldOneSettlesOnTheNearest)
{
    for (const bool flung : {false, true}) {
        Fixture f;
        render::Texture *starting = nullptr;
        f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
        f.node->takeEndingScreenshot(1);
        f.paint();
        render::Texture *ending = f.renderer.lastCapture;
        for (int i = 1; i <= 5; ++i)
            f.node->updateSwipeAnimation(-0.04, 1000 + i * 8);
        for (int i = 1; !flung && i <= 15; ++i)
            f.node->updateSwipeAnimation(-0.0005, 1040 + i * 8);
        f.now = flung ? 1050 : 1300;
        EXPECT_EQ(f.node->endSwipeAnimation(f.now), flung ? 1 : 0) << flung;
        EXPECT_TRUE(f.node->isAnimating());
        EXPECT_FALSE(f.node->isTracking());
        f.now += kSettleMs;
        const auto calls = f.paint();
        EXPECT_EQ(f.finished, 1);
        EXPECT_EQ(Fixture::drawn(calls, flung ? ending : starting)->rect, kViewport);
    }
}

TEST(DeskSwitchNode, TheReleaseCarriesTheFingersSpeedWithoutShootingPast)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    for (int i = 1; i <= 10; ++i)
        f.node->updateSwipeAnimation(-0.09, 1000 + i * 8);
    f.now = 1080 + Node::kResampleLatencyMs;
    int previous = Fixture::drawn(f.paint(), starting)->rect.x();
    f.node->endSwipeAnimation(f.now);
    for (int frame = 0; frame < 60; ++frame) {
        f.now += 16;
        const auto calls = f.paint();
        const auto drawn = Fixture::drawn(calls, starting);
        const int x = drawn ? drawn->rect.x() : -1050;
        EXPECT_LE(x, previous);
        EXPECT_GE(x, -1050);
        previous = x;
    }
    EXPECT_EQ(f.finished, 1);
}

TEST(DeskSwitchNode, AHardFlickLeavesTheRestOfTheWayToAVisibleSlide)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    for (int i = 1; i <= 3; ++i)
        f.node->updateSwipeAnimation(-0.1, 1000 + i * 8);
    f.now = 1024 + Node::kResampleLatencyMs;
    EXPECT_EQ(f.node->endSwipeAnimation(f.now), 1);
    f.now += 100;
    EXPECT_LT(Fixture::drawn(f.paint(), starting)->rect.x(), -Node::kDesksSpacing);
    EXPECT_GT(Fixture::drawn(f.paint(), starting)->rect.x(), -1050 + 100);
    EXPECT_EQ(f.finished, 0);
    f.now += kSettleMs;
    f.paint();
    EXPECT_EQ(f.finished, 1);
}

TEST(DeskSwitchNode, TheFingersCatchASlidingStripWhereItIsShown)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    f.node->startAnimation();
    f.now += 100;
    const int shown = Fixture::drawn(f.paint(), starting)->rect.x();
    f.node->updateSwipeAnimation(0, f.now);
    EXPECT_TRUE(f.node->isTracking());
    EXPECT_FALSE(f.node->isAnimating());
    EXPECT_EQ(Fixture::drawn(f.paint(), starting)->rect.x(), shown);
    f.now += kSettleMs;
    f.paint();
    EXPECT_EQ(f.finished, 0);
}

TEST(DeskSwitchNode, FramesLandOnWholeDevicePixels)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.node->updateSwipeAnimation(-0.1234, 1000);
    f.now = 1000 + Node::kResampleLatencyMs;
    const double x = Fixture::drawn(f.paint("out", 1.5), starting)->target.x();
    EXPECT_DOUBLE_EQ(x * 1.5, std::round(x * 1.5));
    EXPECT_NEAR(x, -0.1234 * 1050, 1);
}

TEST(DeskSwitchNode, ASwipeAsksForTheDeskBeyondTheStripOneAtATime)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 3, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    EXPECT_EQ(f.node->updateSwipeAnimation(-0.97, 1000), std::nullopt);
    f.paint();
    EXPECT_EQ(f.node->updateSwipeAnimation(-0.001, 1008), std::optional<int>(2));
}

TEST(DeskSwitchNode, AHeldSwipeDropsAFrameStillBeingCopied)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 3, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    ASSERT_EQ(f.node->updateSwipeAnimation(-0.999, 1000), std::optional<int>(2));
    f.node->takeEndingScreenshot(2);
    EXPECT_EQ(f.node->endSwipeAnimation(1200), 1);
    EXPECT_TRUE(f.node->endingScreenshotTaken());
    f.paint();
    EXPECT_EQ(f.endingTaken, 1);
}

TEST(DeskSwitchNode, AFlingKeepsTheFrameStillBeingCopiedAndShowsItOnceTaken)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 3, 0, 1, f.frames(&starting), 1000);
    f.node->takeEndingScreenshot(1);
    f.paint();
    for (int i = 1; i <= 4; ++i) {
        if (const auto desk = f.node->updateSwipeAnimation(-0.25, 1000 + i * 8))
            f.node->takeEndingScreenshot(*desk);
    }
    ASSERT_FALSE(f.node->endingScreenshotTaken());
    f.now = 1040;
    EXPECT_EQ(f.node->endSwipeAnimation(f.now), 2);
    f.paint();
    EXPECT_EQ(f.endingTaken, 2);
    render::Texture *ending = f.renderer.lastCapture;
    f.now += kSettleMs;
    EXPECT_EQ(Fixture::drawn(f.paint(), ending)->rect, kViewport);
    EXPECT_EQ(f.finished, 1);
}

TEST(DeskSwitchNode, RemovalShowsBlackForTheRemovedDeskAndKeepsItsWindowsStill)
{
    Fixture f;
    render::Texture *still = nullptr;
    f.node->startRemoval(2, 1, 0, f.frames(&still), {"out"}, 1000);
    auto calls = f.paint();
    ASSERT_EQ(calls.size(), 2);
    EXPECT_EQ(calls[0].kind, "fill");
    EXPECT_EQ(calls[1].texture, still);

    f.node->takeEndingScreenshot(0);
    f.paint();
    render::Texture *ending = f.renderer.lastCapture;
    f.node->startAnimation();
    f.now += Node::kQuickAnimationDurationMs;
    calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, ending)->rect, kViewport);
    EXPECT_EQ(calls.back().texture, still);
    EXPECT_EQ(calls.back().rect, kViewport);
}

TEST(DeskSwitchNode, HittingTheWallNudgesTheDeskAndComesBack)
{
    Fixture f;
    render::Texture *frame = nullptr;
    f.node->hitTheWall(/*goingLeft=*/true, f.frames(&frame), 1000);
    auto calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, frame)->rect.x(), 0);
    f.now += Node::kHitTheWallDurationMs;
    calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, frame)->rect.x(), 100);
    f.now += Node::kHitTheWallDurationMs;
    calls = f.paint();
    EXPECT_EQ(Fixture::drawn(calls, frame)->rect.x(), 0);
    EXPECT_EQ(f.finished, 1);

    f.node->stop();
    EXPECT_FALSE(f.node->isActive());
    EXPECT_TRUE(f.paint().empty());
}

TEST(SlideOutNode, AWindowSentToAnotherDeskSlidesOffTowardsIt)
{
    int64_t now = 0;
    scene::SlideOutNode node([&now] {
        return now;
    });
    FrameRenderer renderer;
    renderer.setOutputContext({"out", kViewport, base::Size(1000, 800), 1, 0, 0, {}});
    Node::Frames frames;
    auto frame = std::make_unique<FrameTexture>(kViewport.size());
    render::Texture *texture = frame.get();
    frames["out"] = std::move(frame);
    node.slideOut(std::move(frames), base::Rect(100, 100, 300, 200), kViewport, /*goingLeft=*/true);
    EXPECT_TRUE(node.isActive());

    node.render(renderer);
    ASSERT_EQ(renderer.calls.size(), 1);
    EXPECT_EQ(renderer.calls[0].texture, texture);
    EXPECT_EQ(renderer.calls[0].rect.x(), 0);

    now = scene::SlideOutNode::kDurationMs / 2;
    renderer.calls.clear();
    node.render(renderer);
    EXPECT_EQ(renderer.calls[0].rect.x(), -100);

    now = scene::SlideOutNode::kDurationMs;
    renderer.calls.clear();
    node.render(renderer);
    EXPECT_EQ(renderer.calls[0].rect.x(), -400);
    EXPECT_FALSE(node.isActive());
}

TEST(SlideOutNode, SlidesNoOutputDrewEndAllTheSame)
{
    int64_t now = 0;
    scene::SlideOutNode node([&now] {
        return now;
    });
    FrameRenderer renderer;
    renderer.setOutputContext({"out", kViewport, base::Size(1000, 800), 1, 0, 0, {}});
    const auto slideOut = [&] {
        Node::Frames frames;
        frames["out"] = std::make_unique<FrameTexture>(kViewport.size());
        node.slideOut(std::move(frames), base::Rect(100, 100, 300, 200), kViewport,
                      /*goingLeft=*/true);
    };
    for (int i = 0; i < 3; ++i) {
        slideOut();
        now += scene::SlideOutNode::kDurationMs;
    }
    slideOut();
    node.render(renderer);
    EXPECT_EQ(renderer.calls.size(), 1);

    slideOut();
    node.finishRetiring();
    EXPECT_FALSE(node.isActive());
}

TEST(DeskSwitchNode, TheDeskContentBelowIsNotDrawnWhereTheNodeCoversIt)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
    const auto drawnUnderneath = [&](const std::string &output) {
        f.renderer.calls.clear();
        f.renderer.setOutputContext({output, kViewport, base::Size(1000, 800), 1, 0, 0, {}});
        f.container->render(f.renderer);
        bool below = false;
        bool above = false;
        for (const auto &call : f.renderer.calls) {
            below |= call.kind == "fill" && call.rect == base::Rect(10, 10, 100, 100);
            above |= call.kind == "fill" && call.rect == base::Rect(20, 20, 100, 100);
        }
        EXPECT_TRUE(above) << output;
        return below;
    };
    EXPECT_FALSE(drawnUnderneath("out"));
    EXPECT_TRUE(drawnUnderneath("other"));
    f.node->stop();
    EXPECT_TRUE(drawnUnderneath("out"));
}

TEST(DeskSwitchNode, TheSceneSkipsItsClearUnderTheCoveringStrip)
{
    Fixture f;
    render::Texture *starting = nullptr;
    f.node->start(Node::Type::Continuous, 2, 0, 1, f.frames(&starting), 1000);
    const auto cleared = [&](const std::string &output) {
        f.renderer.calls.clear();
        f.renderer.setOutputContext({output, kViewport, base::Size(1000, 800), 1, 0, 0, {}});
        f.scene.render(f.renderer);
        return std::any_of(f.renderer.calls.begin(), f.renderer.calls.end(), [](const auto &call) {
            return call.kind == "clear";
        });
    };
    EXPECT_FALSE(cleared("out"));
    EXPECT_TRUE(cleared("other"));
    f.node->stop();
    EXPECT_TRUE(cleared("out"));
}
