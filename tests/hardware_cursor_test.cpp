// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/hardware_display_controller.cc
//           (SetCursor, MoveCursor, NextCursorBuffer)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "base/geometry.h"
#include "backend/drm/drm_cursor.h"
#include "compositor/compositor.h"
#include "compositor/shm_pool.h"
#include "compositor/surface.h"
#include "core/main_loop.h"
#include "cursor/cursor.h"
#include "cursor/output_cursor_router.h"
#include "fake_drm_device.h"
#include "input/input_manager.h"
#include "input/pointer.h"
#include "input/pointer_delegate.h"
#include "input/seat.h"
#include "scene/scene.h"
#include "session/secure_container.h"
#include "session/session_controller.h"
#include "session/session_teardown.h"
#include "shm_test_util.h"
#include "wm/window_manager.h"

namespace cutefish {

namespace {

constexpr uint32_t kCrtcId = 7;

std::vector<uint8_t> opaquePixels(const cutefish::base::Size &size)
{
    return std::vector<uint8_t>(static_cast<size_t>(size.width()) * size.height() * 4, 0xFF);
}

class RecordingSink final : public render::CursorSink
{
public:
    bool setCursorImage(const render::CursorImage &image) override
    {
        ++setImageCalls;
        lastSize = image.size;
        lastHotspot = image.hotspot;
        if (!accept)
            return false;
        visible = true;
        return true;
    }

    void hideCursor() override
    {
        ++hideCalls;
        visible = false;
    }

    void moveCursor(const base::PointF &position) override
    {
        ++moveCalls;
        lastPosition = position;
    }

    bool accept = true;
    bool visible = false;
    int setImageCalls = 0;
    int hideCalls = 0;
    int moveCalls = 0;
    base::Size lastSize;
    base::Point lastHotspot;
    base::PointF lastPosition;
};

class DrmCursorTest : public ::testing::Test
{
protected:
    void SetUp() override { m_device = std::make_unique<testing::FakeDrmDevice>(); }

    std::unique_ptr<testing::FakeDrmDevice> m_device;
};

} // namespace

TEST_F(DrmCursorTest, ADeviceWithoutACursorPlaneHasNoHardwareCursor)
{
    m_device->maxCursor = base::Size();

    EXPECT_EQ(backend::DrmCursor::create(m_device.get(), kCrtcId), nullptr);
}

TEST_F(DrmCursorTest, FailingToAllocateBuffersFallsBackToSoftware)
{
    m_device->dumbBufferFails = true;

    EXPECT_EQ(backend::DrmCursor::create(m_device.get(), kCrtcId), nullptr);
    EXPECT_EQ(m_device->liveDumbBuffers, 0);
}

TEST_F(DrmCursorTest, TheCursorBuffersAndMappingsAreReleasedWithTheCursor)
{
    {
        auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
        ASSERT_TRUE(cursor);
        EXPECT_EQ(m_device->liveDumbBuffers, 2);
        EXPECT_EQ(m_device->liveMappings, 2);
    }

    EXPECT_EQ(m_device->liveDumbBuffers, 0);
    EXPECT_EQ(m_device->liveMappings, 0);
}

TEST_F(DrmCursorTest, AnImageIsUploadedWithItsHotspot)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);

    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(5, 3)));

    EXPECT_TRUE(cursor->isVisible());
    EXPECT_EQ(m_device->cursorCrtc, kCrtcId);
    EXPECT_NE(m_device->cursorHandle, 0u);
    EXPECT_EQ(m_device->cursorSize, m_device->maxCursor);
    EXPECT_EQ(m_device->cursorHotspot, base::Point(5, 3));
}

TEST_F(DrmCursorTest, TheHotspotIsSubtractedFromThePointerPosition)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);
    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(5, 3)));

    cursor->move(base::Point(100, 90));

    // DRM positions the buffer, not the hotspot.
    EXPECT_EQ(m_device->cursorPosition, base::Point(95, 87));
}

TEST_F(DrmCursorTest, AnImageChangeRepositionsForTheNewHotspot)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);
    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(0, 0)));
    cursor->move(base::Point(100, 90));

    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(12, 12)));

    EXPECT_EQ(m_device->cursorPosition, base::Point(88, 78));
}

TEST_F(DrmCursorTest, TheFirstImageStaysWhereThePreviousMastersCursorWas)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);
    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(5, 3)));
    EXPECT_EQ(m_device->moveCursorCalls, 0);
    cursor->move(base::Point(100, 90));
    EXPECT_EQ(m_device->moveCursorCalls, 1);
    EXPECT_EQ(m_device->cursorPosition, base::Point(95, 87));
}

TEST_F(DrmCursorTest, ConsecutiveImagesAlternateBuffers)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);

    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point()));
    const uint32_t first = m_device->cursorHandle;
    auto other = pixels;
    other[0] = 0;
    ASSERT_TRUE(cursor->setImage(other.data(), size, size.width() * 4, base::Point()));

    // Writing into the buffer the display is scanning out would tear the cursor.
    EXPECT_NE(m_device->cursorHandle, first);
}

TEST_F(DrmCursorTest, TheSameImageAgainLeavesThePlaneAlone)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);
    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(5, 3)));
    const int calls = m_device->setCursorCalls;

    EXPECT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(5, 3)));
    EXPECT_EQ(m_device->setCursorCalls, calls);

    EXPECT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(4, 3)));
    EXPECT_EQ(m_device->setCursorCalls, calls + 1);

    cursor->hide();
    EXPECT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(4, 3)));
    EXPECT_EQ(m_device->setCursorCalls, calls + 3);
    EXPECT_TRUE(cursor->isVisible());
}

TEST_F(DrmCursorTest, ATintScalesTheColoursThePlaneShows)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(1, 1);
    const uint8_t pixel[] = {200, 100, 40, 255};
    const auto shown = [&] {
        const auto *data =
                static_cast<const uint8_t *>(m_device->dumbMappings[m_device->cursorHandle]);
        return std::vector<uint8_t>(data, data + 4);
    };

    ASSERT_TRUE(cursor->setImage(pixel, size, 4, base::Point()));
    EXPECT_EQ(shown(), std::vector<uint8_t>({200, 100, 40, 255}));

    // Blue, green, red and alpha in memory.
    cursor->setTint({1.0f, 0.5f, 0.25f});
    EXPECT_EQ(m_device->setCursorCalls, 2);
    EXPECT_EQ(shown(), std::vector<uint8_t>({50, 50, 40, 255}));

    cursor->hide();
    cursor->setTint({});
    EXPECT_EQ(m_device->setCursorCalls, 3);
    ASSERT_TRUE(cursor->setImage(pixel, size, 4, base::Point()));
    EXPECT_EQ(shown(), std::vector<uint8_t>({200, 100, 40, 255}));
}

TEST_F(DrmCursorTest, AnImageLargerThanThePlaneIsRefused)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(128, 128);
    const auto pixels = opaquePixels(size);

    EXPECT_FALSE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point()));
    EXPECT_FALSE(cursor->isVisible());
}

TEST_F(DrmCursorTest, ADriverRejectionIsNotRetried)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    m_device->setCursorFails = true;
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);

    EXPECT_FALSE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point()));
    EXPECT_FALSE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point()));
    EXPECT_FALSE(cursor->isVisible());
}

TEST_F(DrmCursorTest, HidingIsIdempotentAndClearsThePlane)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);
    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point()));
    const int calls = m_device->setCursorCalls;

    cursor->hide();
    EXPECT_EQ(m_device->cursorHandle, 0u);
    EXPECT_EQ(m_device->setCursorCalls, calls + 1);

    cursor->hide();
    EXPECT_EQ(m_device->setCursorCalls, calls + 1);
}

TEST_F(DrmCursorTest, MovingAHiddenCursorCostsNothing)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);

    cursor->move(base::Point(10, 10));

    EXPECT_EQ(m_device->moveCursorCalls, 0);
}

TEST_F(DrmCursorTest, AWaitingDriverTakesOnlyTheNewestPositionOnceTheFlipLands)
{
    // Each move would otherwise block until the flip in flight lands.
    m_device->cursorWaits = true;
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);
    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(5, 3)));

    cursor->setFlipPending(true);
    cursor->move(base::Point(100, 90));
    cursor->move(base::Point(110, 95));
    EXPECT_EQ(m_device->moveCursorCalls, 0);

    cursor->setFlipPending(false);
    EXPECT_EQ(m_device->moveCursorCalls, 1);
    EXPECT_EQ(m_device->cursorPosition, base::Point(105, 92));

    cursor->setFlipPending(true);
    cursor->setFlipPending(false);
    EXPECT_EQ(m_device->moveCursorCalls, 1);

    cursor->move(base::Point(120, 100));
    EXPECT_EQ(m_device->moveCursorCalls, 2);
}

TEST_F(DrmCursorTest, AWaitingDriverTakesOnlyTheNewestImageOnceTheFlipLands)
{
    m_device->cursorWaits = true;
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);
    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(0, 0)));
    cursor->move(base::Point(100, 90));
    const int calls = m_device->setCursorCalls;
    const int moves = m_device->moveCursorCalls;

    cursor->setFlipPending(true);
    auto other = pixels;
    other[0] = 0;
    EXPECT_TRUE(cursor->setImage(other.data(), size, size.width() * 4, base::Point(2, 2)));
    EXPECT_TRUE(cursor->setImage(other.data(), size, size.width() * 4, base::Point(5, 3)));
    cursor->move(base::Point(110, 95));
    EXPECT_EQ(m_device->setCursorCalls, calls);
    EXPECT_EQ(m_device->moveCursorCalls, moves);

    cursor->setFlipPending(false);
    EXPECT_EQ(m_device->setCursorCalls, calls + 1);
    EXPECT_EQ(m_device->cursorHotspot, base::Point(5, 3));
    EXPECT_EQ(m_device->moveCursorCalls, moves + 1);
    EXPECT_EQ(m_device->cursorPosition, base::Point(105, 92));
}

TEST_F(DrmCursorTest, AWaitingDriverStillShowsAHiddenCursorAtOnce)
{
    m_device->cursorWaits = true;
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);

    cursor->setFlipPending(true);
    EXPECT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point()));
    EXPECT_EQ(m_device->setCursorCalls, 1);
    EXPECT_TRUE(cursor->isVisible());
}

TEST_F(DrmCursorTest, OtherDriversMoveTheCursorDuringAFlip)
{
    auto cursor = backend::DrmCursor::create(m_device.get(), kCrtcId);
    ASSERT_TRUE(cursor);
    const cutefish::base::Size size(24, 24);
    const auto pixels = opaquePixels(size);
    ASSERT_TRUE(cursor->setImage(pixels.data(), size, size.width() * 4, base::Point(0, 0)));

    cursor->setFlipPending(true);
    cursor->move(base::Point(100, 90));

    EXPECT_EQ(m_device->moveCursorCalls, 1);
    EXPECT_EQ(m_device->cursorPosition, base::Point(100, 90));
    cursor->setFlipPending(false);
    EXPECT_EQ(m_device->moveCursorCalls, 1);
}

namespace {

struct CursorFixture {
    CursorFixture()
        : compositor(nullptr)
        , seat(&compositor)
        , input(&seat)
    {
        clients = scene.root()->addChild(std::make_unique<scene::Node>());
        seat.setCursorContainer(clients);
        cursor = std::make_unique<cursor::Cursor>(seat, *scene.root(), 24);
        node = scene.root()->children().back();
    }

    scene::Scene scene;
    scene::Node *clients = nullptr;
    compositor::Compositor compositor;
    input::Seat seat;
    input::InputManager input;
    std::unique_ptr<cursor::Cursor> cursor;
    scene::Node *node = nullptr;
    RecordingSink sink;
};

} // namespace

TEST(HardwareCursorFallback, AnAcceptingSinkTakesTheCursorOutOfTheScene)
{
    CursorFixture fixture;
    ASSERT_TRUE(fixture.node->isVisible());

    fixture.cursor->setSink(&fixture.sink);

    EXPECT_TRUE(fixture.cursor->isHardware());
    EXPECT_FALSE(fixture.node->isVisible());
    EXPECT_EQ(fixture.sink.setImageCalls, 1);
    EXPECT_EQ(fixture.sink.lastSize, base::Size(24, 24));
}

TEST(HardwareCursorFallback, AnUnchangedThemeDoesNotReprogramThePlane)
{
    CursorFixture fixture;
    fixture.cursor->setTheme("default", 24);
    fixture.cursor->setSink(&fixture.sink);
    ASSERT_EQ(fixture.sink.setImageCalls, 1);

    fixture.cursor->setTheme("default", 24);

    EXPECT_EQ(fixture.sink.setImageCalls, 1);
    EXPECT_EQ(fixture.sink.hideCalls, 0);
    EXPECT_TRUE(fixture.sink.visible);
}

TEST(HardwareCursorFallback, MotionOnAHardwareCursorDoesNotTouchTheScene)
{
    CursorFixture fixture;
    fixture.cursor->setSink(&fixture.sink);
    const base::Point before = fixture.node->position();

    fixture.input.warpPointer({100, 90});

    EXPECT_TRUE(base::fuzzyCompare(fixture.sink.lastPosition, base::PointF(100, 90)))
            << fixture.sink.lastPosition;
    EXPECT_EQ(fixture.node->position(), before);
}

TEST(HardwareCursorFallback, ARefusingSinkLeavesTheCursorComposited)
{
    CursorFixture fixture;
    fixture.sink.accept = false;

    fixture.cursor->setSink(&fixture.sink);

    EXPECT_FALSE(fixture.cursor->isHardware());
    EXPECT_TRUE(fixture.node->isVisible());
    fixture.input.warpPointer({100, 90});
    EXPECT_EQ(fixture.node->position(),
              base::Point(100, 90) - cursor::load(input::CursorShape::Default, 24).front().hotspot);
}

TEST(HardwareCursorFallback, AHiddenCursorIsTakenOffThePlane)
{
    CursorFixture fixture;
    fixture.cursor->setSink(&fixture.sink);
    ASSERT_TRUE(fixture.sink.visible);

    fixture.seat.setCompositorCursorShape(input::CursorShape::Hidden);

    EXPECT_FALSE(fixture.sink.visible);
    EXPECT_FALSE(fixture.cursor->isHardware());
    EXPECT_FALSE(fixture.node->isVisible());
}

TEST(HardwareCursorFallback, DroppingTheSinkPutsTheCursorBackInTheScene)
{
    CursorFixture fixture;
    fixture.cursor->setSink(&fixture.sink);
    ASSERT_TRUE(fixture.cursor->isHardware());

    fixture.cursor->setSink(nullptr);

    EXPECT_FALSE(fixture.cursor->isHardware());
    EXPECT_TRUE(fixture.node->isVisible());
    EXPECT_EQ(fixture.sink.hideCalls, 1);
}

TEST(HardwareCursorFallback, AShapeChangeReuploadsTheImage)
{
    CursorFixture fixture;
    fixture.cursor->setSink(&fixture.sink);
    const int uploads = fixture.sink.setImageCalls;

    fixture.seat.setCompositorCursorShape(input::CursorShape::EwResize);

    EXPECT_GT(fixture.sink.setImageCalls, uploads);
    EXPECT_TRUE(fixture.cursor->isHardware());
}

TEST(CursorScaling, EveryScaleUsesThePlaneWithDeviceImageAndHotspot)
{
    scene::Scene scene;
    compositor::Compositor compositor(scene.root());
    input::Seat seat(&compositor);
    cursor::Cursor cursor(seat, *scene.root(), 24);
    RecordingSink sink;
    cursor.setSink(&sink);
    for (double scale : {1.0, 1.25, 1.5, 1.75, 2.0, 1.0}) {
        cursor.setScale(scale);
        seat.updateCursorPosition({300, 200});
        EXPECT_TRUE(cursor.isHardware()) << scale;
        EXPECT_TRUE(sink.visible) << scale;
        EXPECT_TRUE(base::fuzzyCompare(sink.lastPosition, base::PointF(300, 200))) << sink.lastPosition;
        const auto frames = cursor::load(seat.compositorCursorShape(), 24 * int(std::ceil(scale)));
        ASSERT_FALSE(frames.empty());
        const double ratio = scale / std::ceil(scale);
        const base::Size artwork = frames.front().image.size();
        EXPECT_EQ(sink.lastSize, base::Size(int(std::floor(artwork.width() * ratio)),
                                            int(std::floor(artwork.height() * ratio))))
                << scale;
        EXPECT_EQ(sink.lastHotspot, base::Point(int(std::floor(frames.front().hotspot.x() * ratio)),
                                                int(std::floor(frames.front().hotspot.y() * ratio))))
                << scale;
    }
    sink.accept = false;
    cursor.setScale(1.5);
    EXPECT_FALSE(cursor.isHardware());
    EXPECT_FALSE(sink.visible);
    cursor.setSink(nullptr);
}

TEST(CursorScaling, AScaledOutputGetsTheScaleOnce)
{
    scene::Scene scene;
    compositor::Compositor compositor(scene.root());
    input::Seat seat(&compositor);
    cursor::Cursor cursor(seat, *scene.root(), 24);
    RecordingSink output;
    cursor::OutputCursorRouter router;
    router.setTargets({{{0, 0, 1080, 720},
                        &output,
                        display::MirrorTransform({0, 0, 1080, 720}, {0, 0, 2160, 1440},
                                                 display::DisplayRotation::Normal)}});
    cursor.setSink(&router);
    cursor.setScale(2);
    seat.updateCursorPosition({400.5, 250});
    ASSERT_TRUE(cursor.isHardware());
    EXPECT_TRUE(base::fuzzyCompare(output.lastPosition, base::PointF(801, 500))) << output.lastPosition;
    seat.updateCursorPosition({1000, 700});
    EXPECT_TRUE(base::fuzzyCompare(output.lastPosition, base::PointF(2000, 1400)))
            << output.lastPosition;
    cursor.setSink(nullptr);
}

namespace {

struct LockCursorFixture {
    LockCursorFixture()
        : compositor(surfaces)
        , seat(&compositor)
        , input(&seat)
        , manager(&compositor, &seat)
        , secure(secureNode)
        , teardown(&seat, &manager)
        , controller(&secure, &teardown)
    {
        scene.setSize({800, 600});
        manager.setOutputBounds({0, 0, 800, 600});
        seat.setSessionGate(&controller);
        manager.setSessionGate(&controller);
        controller.setOutputs({{"DP-1", base::Rect(0, 0, 800, 600)}});
        seat.setCursorContainer(clients);
        cursor = std::make_unique<cursor::Cursor>(seat, *cursorRoot, 24);
        node = cursorRoot->children().back();
        teardown.setCursorReset([this] {
            cursor->refresh();
        });
    }

    ~LockCursorFixture() { seat.shutdown(); }

    compositor::Surface *makeSurface(const base::Size &size)
    {
        owned.push_back(compositor.createSurface());
        compositor::Surface *surface = owned.back().get();
        auto buffer = pool->createBuffer(size, 0, size.width() * 4,
                                         render::PixelFormat::Bgrx8888,
                                         nullptr);
        surface->attach(buffer, {});
        surface->damage(base::Rect(size));
        surface->commit();
        return surface;
    }

    scene::Scene scene;
    scene::Node *surfaces = scene.root()->addChild(std::make_unique<scene::Node>());
    scene::Node *secureNode = scene.root()->addChild(std::make_unique<scene::Node>());
    scene::Node *cursorRoot = scene.root()->addChild(std::make_unique<scene::Node>());
    scene::Node *clients = cursorRoot->addChild(std::make_unique<scene::Node>());
    compositor::Compositor compositor;
    input::Seat seat;
    input::InputManager input;
    wm::WindowManager manager;
    session::SecureContainer secure;
    session::SessionTeardown teardown;
    session::SessionController controller;
    std::unique_ptr<cursor::Cursor> cursor;
    scene::Node *node = nullptr;
    RecordingSink sink;
    std::vector<std::unique_ptr<compositor::Surface>> owned;
    std::shared_ptr<compositor::ShmPool> pool = testsupport::makePool(1 << 20);
};

class PermissivePointerDelegate : public input::PointerDelegate
{
public:
    void onPointerDestroying(input::Pointer *) override { }

    bool canAcceptPointerEventsForSurface(compositor::Surface *) const override { return true; }

    void onPointerEnter(compositor::Surface *, const base::PointF &, input::ButtonFlags) override { }

    void onPointerLeave(compositor::Surface *) override { }

    void onPointerMotion(input::Timestamp, const base::PointF &) override { }

    void onPointerButton(input::Timestamp, input::ButtonFlags, bool) override { }

    void onPointerScroll(input::Timestamp, const base::PointF &, bool) override { }

    void onFingerScrollStop(input::Timestamp) override { }

    void onPointerFrame() override { }
};

} // namespace

TEST(SessionLockCursor, LockingTakesTheClientCursorSurfaceOutOfTheScene)
{
    LockCursorFixture fixture;
    PermissivePointerDelegate delegate;
    input::Pointer pointer(&delegate, &fixture.seat);

    compositor::Surface *window = fixture.makeSurface({200, 150});
    window->setPosition({100, 100});
    fixture.seat.onPointerMotion(input::Timestamp(std::chrono::milliseconds(1)), base::PointF(150, 150));
    ASSERT_EQ(window, pointer.focusedSurface());

    compositor::Surface *clientCursor = fixture.makeSurface({16, 16});
    pointer.setCursor(clientCursor, base::Point(2, 2));
    ASSERT_EQ(fixture.clients, clientCursor->node()->parent());
    ASSERT_TRUE(fixture.seat.hasClientCursorRequest());

    ASSERT_TRUE(fixture.controller.requestLock());

    EXPECT_NE(fixture.clients, clientCursor->node()->parent());
    EXPECT_EQ(nullptr, pointer.focusedSurface());
    EXPECT_FALSE(fixture.seat.hasClientCursorRequest());
    EXPECT_FALSE(fixture.seat.clientCursorShape().has_value());
    EXPECT_TRUE(fixture.node->isVisible());
}

TEST(SessionLockCursor, LockingReprogramsTheHardwareCursorPlane)
{
    LockCursorFixture fixture;
    PermissivePointerDelegate delegate;
    input::Pointer pointer(&delegate, &fixture.seat);
    fixture.cursor->setSink(&fixture.sink);
    ASSERT_TRUE(fixture.cursor->isHardware());

    compositor::Surface *window = fixture.makeSurface({200, 150});
    window->setPosition({100, 100});
    fixture.seat.onPointerMotion(input::Timestamp(std::chrono::milliseconds(1)), base::PointF(150, 150));
    ASSERT_EQ(window, pointer.focusedSurface());
    compositor::Surface *clientCursor = fixture.makeSurface({16, 16});
    pointer.setCursor(clientCursor, base::Point(2, 2));
    const int uploadsBefore = fixture.sink.setImageCalls;

    ASSERT_TRUE(fixture.controller.requestLock());

    EXPECT_GT(fixture.sink.setImageCalls, uploadsBefore);
    EXPECT_TRUE(fixture.sink.visible);
    EXPECT_TRUE(fixture.cursor->isHardware());
}

TEST(SessionLockCursor, WakingWithARefusingPlaneStillLeavesNoSessionCursor)
{
    LockCursorFixture fixture;
    fixture.cursor->setSink(&fixture.sink);
    ASSERT_TRUE(fixture.cursor->isHardware());

    ASSERT_TRUE(fixture.controller.requestLock());
    fixture.sink.accept = false;
    fixture.cursor->refresh();

    EXPECT_FALSE(fixture.cursor->isHardware());
    EXPECT_FALSE(fixture.sink.visible);
    EXPECT_TRUE(fixture.node->isVisible());
}

} // namespace cutefish
