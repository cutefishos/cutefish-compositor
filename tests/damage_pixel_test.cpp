// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <GLES3/gl3.h>
#include <gtest/gtest.h>

#include "egl_test_util.h"
#include "render/damage.h"
#include "scene/scene.h"
#include "scene/surface_node.h"
#include "scene/window_animation.h"
#include "base/geometry.h"
#include "ui/bitmap.h"

using namespace cutefish;

TEST(DamagePixels, PartialFramesMatchCompleteCaptureAcrossScaleAndRotation)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const base::Size size(100, 100);
    const EGLint attributes[] = {EGL_WIDTH, 100, EGL_HEIGHT, 100, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, size);
    for (double scale : {1.0, 1.25, 1.5, 1.75}) {
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            scene::Scene scene;
            scene.setSize({200, 200});
            auto *moving = scene.root()->addChild(
                    std::make_unique<scene::ColorNode>(render::Color {1, 0, 0, 1}));
            auto *glass = scene.root()->addChild(
                    std::make_unique<scene::ColorNode>(render::Color {0, 1, 0, 0.5}));
            glass->setGeometry({-30, 20, 40, 40});
            render::OutputRenderContext context {"test", base::Rect(-40, 10, 100, 100), size, scale,
                                                 rotation};
            renderer->setOutputContext(context);
            ASSERT_TRUE(renderer->beginFrame(target.get()));
            scene::DamageTracker tracker;
            for (int frame = 0; frame < 12; ++frame) {
                moving->setGeometry({-35 + frame, 15 + frame, 10 + frame, 15});
                if (frame == 5)
                    moving->setVisible(false);
                if (frame == 6)
                    moving->setVisible(true);
                const auto damage = tracker.update(*scene.root(), context.logicalViewport);
                const auto physical = render::physicalDamage(damage, context, size);
                render::EglRendererTestAccess::applyRepaint(*renderer, physical);
                renderer->clear(scene.background());
                scene.root()->render(*renderer);
                ui::Bitmap partial(size, ui::Bitmap::Format::Rgba8888Premultiplied);
                glReadPixels(0, 0, 100, 100, GL_RGBA, GL_UNSIGNED_BYTE, partial.bits());
                partial = partial.mirrored(false, true).converted(ui::Bitmap::Format::Argb32Premultiplied);
                const auto complete = renderer->captureFrame([&] {
                    scene.render(*renderer);
                });
                EXPECT_EQ(partial, complete)
                        << "scale=" << scale << " rotation=" << rotation << " frame=" << frame;
                EXPECT_TRUE(glIsEnabled(GL_SCISSOR_TEST));
            }
            renderer->cancelFrame();
        }
    }
    renderer->releaseTarget(target.get());
}

TEST(DamagePixels, MissingBufferAgeForcesCompleteRepaint)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const EGLint attributes[] = {EGL_WIDTH, 20, EGL_HEIGHT, 20, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, base::Size(20, 20));
    renderer->setOutputContext({"test", base::Rect(0, 0, 20, 20), {20, 20}, 1});
    ASSERT_TRUE(renderer->beginFrame(target.get()));
    renderer->prepareDamage(base::Rect(2, 2, 1, 1), {});
    renderer->clear({0, 0, 1, 1});
    uint8_t pixel[4];
    glReadPixels(19, 19, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    EXPECT_EQ(pixel[2], 255);
    renderer->cancelFrame();
    EXPECT_FALSE(glIsEnabled(GL_SCISSOR_TEST));
    renderer->releaseTarget(target.get());
}

TEST(DamagePixels, FilteredTextureDamageMatchesFullFrame)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const EGLint attributes[] = {EGL_WIDTH, 100, EGL_HEIGHT, 100, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, base::Size(100, 100));
    for (double scale : {1.25, 1.5, 1.75, 2.0}) {
        scene::Scene scene;
        scene.setSize({100, 100});
        auto *node = static_cast<scene::SurfaceNode *>(
                scene.root()->addChild(std::make_unique<scene::SurfaceNode>()));
        node->setGeometry({3, 5, 32, 32});
        render::OutputRenderContext context {"test", base::Rect(0, 0, 100, 100), {100, 100}, scale};
        renderer->setOutputContext(context);
        ASSERT_TRUE(renderer->beginFrame(target.get()));
        std::vector<uint8_t> pixels(32 * 32 * 4, 255);
        std::shared_ptr<render::Texture> texture = renderer->createTexture({32, 32}, pixels.data());
        node->setTexture(texture);
        scene::DamageTracker tracker;
        for (int frame = 0; frame < 12; ++frame) {
            const int x = 5 + frame, y = 8;
            pixels[(y * 32 + x) * 4] = 0;
            pixels[(y * 32 + x) * 4 + 1] = 0;
            ASSERT_TRUE(renderer->updateTexture(texture.get(), pixels.data(), 128,
                                                render::PixelFormat::Rgba8888));
            node->notifyDamage(base::Rect(x, y, 1, 1));
            const auto damage = tracker.update(*scene.root(), context.logicalViewport);
            render::EglRendererTestAccess::applyRepaint(
                    *renderer, render::physicalDamage(damage, context, {100, 100}));
            renderer->clear(scene.background());
            scene.root()->render(*renderer);
            ui::Bitmap partial(100, 100, ui::Bitmap::Format::Rgba8888Premultiplied);
            glReadPixels(0, 0, 100, 100, GL_RGBA, GL_UNSIGNED_BYTE, partial.bits());
            partial = partial.mirrored(false, true).converted(ui::Bitmap::Format::Argb32Premultiplied);
            const auto complete = renderer->captureFrame([&] {
                scene.render(*renderer);
            });
            EXPECT_EQ(partial, complete) << "scale=" << scale << " frame=" << frame;
        }
        renderer->cancelFrame();
    }
    renderer->releaseTarget(target.get());
}

// A native-density texture snaps its origin and size to pixels separately, so at
// 1.5 a 33-unit surface at x = 3 covers pixels 5..54 while its rect encloses 4..53.
TEST(DamagePixels, MovingNativeDensitySurfaceLeavesNoColumnBehind)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const EGLint attributes[] = {EGL_WIDTH, 100, EGL_HEIGHT, 100, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, base::Size(100, 100));
    for (double scale : {1.25, 1.5, 1.75}) {
        for (uint32_t rotation = 0; rotation < 4; ++rotation) {
            scene::Scene scene;
            scene.setSize({100, 100});
            auto *node = static_cast<scene::SurfaceNode *>(
                    scene.root()->addChild(std::make_unique<scene::SurfaceNode>()));
            const base::Size logical(33, 25);
            const base::Size device(base::roundToInt(logical.width() * scale), base::roundToInt(logical.height() * scale));
            std::vector<uint8_t> pixels(device.width() * device.height() * 4, 255);
            render::OutputRenderContext context {"test", base::Rect(0, 0, 100, 100), {100, 100},
                                                 scale, rotation};
            renderer->setOutputContext(context);
            ASSERT_TRUE(renderer->beginFrame(target.get()));
            node->setTexture(renderer->createTexture(device, pixels.data()));
            scene::DamageTracker tracker;
            for (int frame = 0; frame < 8; ++frame) {
                node->setGeometry({base::Point(3 + frame, 5 + frame), logical});
                const auto damage = tracker.update(*scene.root(), context.logicalViewport);
                render::EglRendererTestAccess::applyRepaint(
                        *renderer, render::physicalDamage(damage, context, {100, 100}));
                renderer->clear(scene.background());
                scene.root()->render(*renderer);
                ui::Bitmap partial(100, 100, ui::Bitmap::Format::Rgba8888Premultiplied);
                glReadPixels(0, 0, 100, 100, GL_RGBA, GL_UNSIGNED_BYTE, partial.bits());
                partial = partial.mirrored(false, true).converted(ui::Bitmap::Format::Argb32Premultiplied);
                const auto complete = renderer->captureFrame([&] {
                    scene.render(*renderer);
                });
                EXPECT_EQ(partial, complete)
                        << "scale=" << scale << " rotation=" << rotation << " frame=" << frame;
            }
            renderer->cancelFrame();
        }
    }
    renderer->releaseTarget(target.get());
}

TEST(DamagePixels, BlurInputHaloMatchesCompleteFrame)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const EGLint attributes[] = {EGL_WIDTH, 400, EGL_HEIGHT, 400, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, base::Size(400, 400));
    for (double scale : {1.0, 1.25, 1.5}) {
        scene::Scene scene;
        scene.setSize({400, 400});
        auto *moving = scene.root()->addChild(
                std::make_unique<scene::ColorNode>(render::Color {1, 1, 1, 1}));
        auto *blur = static_cast<scene::SurfaceNode *>(
                scene.root()->addChild(std::make_unique<scene::SurfaceNode>()));
        blur->setGeometry({150, 150, 32, 32});
        blur->setBackgroundBlur({0, 0, 32, 32}, 30);
        render::OutputRenderContext context {"test", base::Rect(0, 0, 400, 400), {400, 400}, scale};
        renderer->setOutputContext(context);
        ASSERT_TRUE(renderer->beginFrame(target.get()));
        std::vector<uint8_t> pixels(32 * 32 * 4, 0);
        blur->setTexture(renderer->createTexture({32, 32}, pixels.data()));
        scene::DamageTracker tracker;
        for (int frame = 0; frame < 4; ++frame) {
            moving->setGeometry({120 + frame * 10, 130, 20, 90});
            const auto damage = tracker.update(*scene.root(), context.logicalViewport, scale);
            const auto physical = render::coverSamples(
                    render::physicalDamage(damage, context, {400, 400}), tracker.samples(),
                    context, {400, 400});
            if (frame)
                EXPECT_LT(physical.width() * physical.height(), 400 * 400);
            render::EglRendererTestAccess::applyRepaint(*renderer, physical);
            renderer->clear(scene.background());
            scene.root()->render(*renderer);
            ui::Bitmap partial(400, 400, ui::Bitmap::Format::Rgba8888Premultiplied);
            glReadPixels(0, 0, 400, 400, GL_RGBA, GL_UNSIGNED_BYTE, partial.bits());
            partial = partial.mirrored(false, true).converted(ui::Bitmap::Format::Argb32Premultiplied);
            const auto complete = renderer->captureFrame([&] {
                scene.render(*renderer);
            });
            EXPECT_EQ(partial, complete) << "scale=" << scale << " frame=" << frame;
        }
        renderer->cancelFrame();
    }
    renderer->releaseTarget(target.get());
}

TEST(DamagePixels, ARepaintReachingABlurRedrawsItFromAllItReads)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const base::Size size(400, 400);
    const EGLint attributes[] = {EGL_WIDTH, 400, EGL_HEIGHT, 400, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, size);
    scene::Scene scene;
    scene.setSize(size);
    // A still backdrop, which the finished frame shows blurred inside the blur.
    auto *strip = scene.root()->addChild(
            std::make_unique<scene::ColorNode>(render::Color {1, 1, 1, 1}));
    strip->setGeometry({160, 110, 12, 100});
    auto *blur = static_cast<scene::SurfaceNode *>(
            scene.root()->addChild(std::make_unique<scene::SurfaceNode>()));
    blur->setGeometry({150, 150, 32, 32});
    blur->setBackgroundBlur({0, 0, 32, 32}, 30);
    // Changes beyond its halo on either side, whose bounding repaint crosses it.
    auto *left = scene.root()->addChild(
            std::make_unique<scene::ColorNode>(render::Color {1, 0, 0, 1}));
    auto *right = scene.root()->addChild(
            std::make_unique<scene::ColorNode>(render::Color {0, 0, 1, 1}));
    const render::OutputRenderContext context {"test", base::Rect(base::Point(), size), size, 1};
    renderer->setOutputContext(context);
    ASSERT_TRUE(renderer->beginFrame(target.get()));
    std::vector<uint8_t> pixels(32 * 32 * 4, 0);
    blur->setTexture(renderer->createTexture({32, 32}, pixels.data()));
    scene::DamageTracker tracker;
    for (int frame = 0; frame < 4; ++frame) {
        left->setGeometry({20, 170 + frame * 4, 10, 10});
        right->setGeometry({370, 170 - frame * 4, 10, 10});
        const auto damage = tracker.update(*scene.root(), context.logicalViewport);
        const base::Rect physical = render::physicalDamage(damage, context, size);
        const base::Rect repaint = render::coverSamples(physical, tracker.samples(), context, size);
        if (frame) {
            EXPECT_NE(repaint, physical);
            EXPECT_LT(repaint.width() * repaint.height(), size.width() * size.height());
        }
        render::EglRendererTestAccess::applyRepaint(*renderer, repaint);
        renderer->clear(scene.background());
        scene.root()->render(*renderer);
        ui::Bitmap partial(size, ui::Bitmap::Format::Rgba8888Premultiplied);
        glReadPixels(0, 0, 400, 400, GL_RGBA, GL_UNSIGNED_BYTE, partial.bits());
        partial = partial.mirrored(false, true).converted(ui::Bitmap::Format::Argb32Premultiplied);
        const auto complete = renderer->captureFrame([&] {
            scene.render(*renderer);
        });
        EXPECT_EQ(partial, complete) << "frame=" << frame;
    }
    renderer->cancelFrame();
    renderer->releaseTarget(target.get());
}

// A blur whose backdrop has not changed draws the result it kept, so a repaint
// over it need not take in all it reads.
TEST(DamagePixels, AKeptBlurMatchesTheCompleteFrame)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const base::Size size(400, 400);
    const EGLint attributes[] = {EGL_WIDTH, 400, EGL_HEIGHT, 400, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, size);
    for (double scale : {1.0, 1.5}) {
        for (bool glassy : {false, true}) {
            scene::Scene scene;
            scene.setSize(size);
            auto *strip = scene.root()->addChild(
                    std::make_unique<scene::ColorNode>(render::Color {1, 1, 1, 1}));
            auto *blur = static_cast<scene::SurfaceNode *>(
                    scene.root()->addChild(std::make_unique<scene::SurfaceNode>()));
            blur->setGeometry({150, 150, 64, 48});
            render::GlassParams glass;
            glass.enabled = glassy;
            blur->setBackgroundBlur({0, 0, 64, 48}, 12, glass);
            auto *above = scene.root()->addChild(
                    std::make_unique<scene::ColorNode>(render::Color {1, 0, 0, 1}));
            const render::OutputRenderContext context {"test", base::Rect(base::Point(), size), size,
                                                       scale};
            renderer->setOutputContext(context);
            ASSERT_TRUE(renderer->beginFrame(target.get()));
            std::vector<uint8_t> pixels(64 * 48 * 4, 0);
            blur->setTexture(renderer->createTexture({64, 48}, pixels.data()));
            scene::DamageTracker tracker;
            const auto scope = uint64_t(reinterpret_cast<uintptr_t>(&tracker));
            const auto kept = [&](uint64_t owner) {
                return renderer->keepsBackdrop(scope, owner);
            };
            for (int frame = 0; frame < 6; ++frame) {
                // The backdrop moves on the fourth frame only.
                strip->setGeometry({frame == 3 ? 170 : 160, 110, 12, 100});
                above->setGeometry({155 + frame * 6, 160, 8, 8});
                const auto damage = tracker.update(*scene.root(), context.logicalViewport, scale);
                const base::Rect physical = render::physicalDamage(damage, context, size);
                const base::Rect repaint = render::coverSamples(physical, tracker.samples(kept),
                                                           context, size);
                if (frame == 2 || frame == 5)
                    EXPECT_EQ(repaint, physical) << "scale=" << scale << " frame=" << frame;
                if (frame == 3 || frame == 4)
                    EXPECT_NE(repaint, physical) << "scale=" << scale << " frame=" << frame;
                render::EglRendererTestAccess::applyRepaint(*renderer, repaint);
                {
                    const render::BackdropPass backdrops(*renderer, scope,
                                                         tracker.freshBackdrops(kept));
                    renderer->clear(scene.background());
                    scene.root()->render(*renderer);
                }
                ui::Bitmap partial(size, ui::Bitmap::Format::Rgba8888Premultiplied);
                glReadPixels(0, 0, 400, 400, GL_RGBA, GL_UNSIGNED_BYTE, partial.bits());
                partial = partial.mirrored(false, true).converted(ui::Bitmap::Format::Argb32Premultiplied);
                const auto complete = renderer->captureFrame([&] {
                    scene.render(*renderer);
                });
                // Glass shading rounds one rim pixel differently in a capture.
                int worst = 0;
                for (int y = 0; y < size.height(); ++y) {
                    for (int x = 0; x < size.width(); ++x) {
                        const uint32_t p = partial.pixel(x, y), c = complete.pixel(x, y);
                        for (int shift = 0; shift < 32; shift += 8)
                            worst = std::max(worst, std::abs(int(p >> shift & 0xff) - int(c >> shift & 0xff)));
                    }
                }
                EXPECT_LE(worst, glassy ? 1 : 0)
                        << "scale=" << scale << " glass=" << glassy << " frame=" << frame;
            }
            renderer->cancelFrame();
        }
    }
    renderer->releaseTarget(target.get());
}

// A blur whose backdrop changes beside it redoes only the part of its kept
// result that reads the change, and matches a blur drawn whole.
TEST(DamagePixels, APartlyRedoneBlurMatchesTheCompleteFrame)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const base::Size size(400, 400);
    const EGLint attributes[] = {EGL_WIDTH, 400, EGL_HEIGHT, 400, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, size);
    for (double scale : {1.0, 1.25, 1.5}) {
        for (bool glassy : {false, true}) {
            scene::Scene scene;
            scene.setSize(size);
            const auto add = [&](const render::Color &color, const base::Rect &geometry) {
                auto *node = scene.root()->addChild(std::make_unique<scene::ColorNode>(color));
                node->setGeometry(geometry);
                return node;
            };
            add({1, 0, 0, 1}, {50, 50, 40, 40});
            add({0, 1, 0, 1}, {120, 80, 70, 60});
            add({0, 0, 1, 1}, {70, 150, 60, 50});
            // Outside the blur, inside what it reads: one beside it, one under
            // it; and one inside it, which the lip of a glass refracts from afar.
            auto *right = add({1, 1, 1, 1}, {252, 60, 12, 30});
            auto *bottom = add({1, 1, 0, 1}, {60, 232, 30, 12});
            auto *inside = add({0, 1, 1, 1}, {186, 60, 10, 30});
            auto *blur = static_cast<scene::SurfaceNode *>(
                    scene.root()->addChild(std::make_unique<scene::SurfaceNode>()));
            blur->setGeometry({40, 40, 200, 180});
            render::GlassParams glass;
            glass.enabled = glassy;
            blur->setBackgroundBlur({0, 0, 200, 180}, 12, glass);
            const render::OutputRenderContext context {"test", base::Rect(base::Point(), size), size,
                                                       scale};
            renderer->setOutputContext(context);
            ASSERT_TRUE(renderer->beginFrame(target.get()));
            std::vector<uint8_t> pixels(200 * 180 * 4, 0);
            blur->setTexture(renderer->createTexture({200, 180}, pixels.data()));
            scene::DamageTracker tracker;
            const auto scope = uint64_t(reinterpret_cast<uintptr_t>(&tracker));
            const auto kept = [&](uint64_t owner) {
                return renderer->keepsBackdrop(scope, owner);
            };
            for (int frame = 0; frame < 9; ++frame) {
                if (frame % 3 == 0)
                    bottom->setGeometry({60 + frame * 10, 232, 30, 12});
                else if (frame % 3 == 1)
                    right->setGeometry({252, 60 + frame * 9, 12, 30});
                else
                    inside->setGeometry({186, 60 + frame * 9, 10, 30});
                const auto damage = tracker.update(*scene.root(), context.logicalViewport, scale);
                const base::Rect physical = render::physicalDamage(damage, context, size);
                const base::Rect repaint = render::coverSamples(physical, tracker.samples(kept),
                                                           context, size);
                const auto fresh = tracker.freshBackdrops(kept);
                if (frame > 1)
                    EXPECT_TRUE(fresh.size() == 1 && !fresh.front().changed.isEmpty())
                            << "scale=" << scale << " glass=" << glassy << " frame=" << frame;
                // A plain blur redone inside reads all of one this small.
                if (frame > 1 && (glassy || frame % 3 != 2))
                    EXPECT_NE(repaint, render::coverSamples(physical, tracker.samples(), context, size))
                            << "scale=" << scale << " glass=" << glassy << " frame=" << frame;
                render::EglRendererTestAccess::applyRepaint(*renderer, repaint);
                {
                    const render::BackdropPass backdrops(*renderer, scope, fresh);
                    renderer->clear(scene.background());
                    scene.root()->render(*renderer);
                }
                ui::Bitmap partial(size, ui::Bitmap::Format::Rgba8888Premultiplied);
                glReadPixels(0, 0, 400, 400, GL_RGBA, GL_UNSIGNED_BYTE, partial.bits());
                partial = partial.mirrored(false, true).converted(ui::Bitmap::Format::Argb32Premultiplied);
                const auto complete = renderer->captureFrame([&] {
                    scene.render(*renderer);
                });
                // Glass shading rounds one rim pixel differently in a capture.
                int worst = 0;
                for (int y = 0; y < size.height(); ++y) {
                    for (int x = 0; x < size.width(); ++x) {
                        const uint32_t p = partial.pixel(x, y), c = complete.pixel(x, y);
                        for (int shift = 0; shift < 32; shift += 8)
                            worst = std::max(worst, std::abs(int(p >> shift & 0xff) - int(c >> shift & 0xff)));
                    }
                }
                EXPECT_LE(worst, glassy ? 1 : 0)
                        << "scale=" << scale << " glass=" << glassy << " frame=" << frame;
            }
            renderer->cancelFrame();
        }
    }
    renderer->releaseTarget(target.get());
}

// A window's blur, drawn into the window's texture, that the output redoes in
// part beside a change: the texture draws only that part again, and the
// frame matches one drawn whole.
TEST(DamagePixels, AWindowBlurRedoneInPartMatchesTheCompleteFrame)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const base::Size size(400, 400);
    const EGLint attributes[] = {EGL_WIDTH, 400, EGL_HEIGHT, 400, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, size);
    for (double scale : {1.0, 1.5}) {
        scene::Scene scene;
        scene.setSize(size);
        const auto add = [&](const render::Color &color, const base::Rect &geometry) {
            auto *node = scene.root()->addChild(std::make_unique<scene::ColorNode>(color));
            node->setGeometry(geometry);
            return node;
        };
        add({1, 0, 0, 1}, {60, 60, 40, 40});
        add({0, 0, 1, 1}, {110, 200, 60, 50});
        // Beside the window, under it, inside what its blur reads.
        auto *beside = add({1, 1, 0, 1}, {256, 60, 30, 30});
        auto *window = static_cast<scene::WindowAnimation *>(
                scene.root()->addChild(std::make_unique<scene::WindowAnimation>()));
        window->setGeometry({40, 40, 200, 240});
        window->finish();
        auto *content = static_cast<scene::SurfaceNode *>(
                window->addChild(std::make_unique<scene::SurfaceNode>()));
        content->setGeometry({0, 0, 200, 240});
        content->setBackgroundBlur({0, 0, 200, 240}, 12);
        const render::OutputRenderContext context {"test", base::Rect(base::Point(), size), size,
                                                   scale};
        renderer->setOutputContext(context);
        ASSERT_TRUE(renderer->beginFrame(target.get()));
        std::vector<uint8_t> pixels(200 * 240 * 4, 0);
        std::shared_ptr<render::Texture> texture = renderer->createTexture({200, 240}, pixels.data());
        content->setTexture(texture);
        scene::DamageTracker tracker;
        const auto scope = uint64_t(reinterpret_cast<uintptr_t>(&tracker));
        const auto kept = [&](uint64_t owner) {
            return renderer->keepsBackdrop(scope, owner);
        };
        for (int frame = 0; frame < 8; ++frame) {
            beside->setGeometry({256, 60 + frame * 17, 30, 30});
            // A row inside the window changes on its own now and then, as a cursor blinks.
            if (frame % 3 == 2) {
                for (int y = 30; y < 40; ++y) {
                    for (int x = 10; x < 20; ++x)
                        pixels[(y * 200 + x) * 4 + 3] = pixels[(y * 200 + x) * 4 + 3] ? 0 : 255;
                }
                ASSERT_TRUE(renderer->updateTexture(texture.get(), pixels.data(), 800,
                                                    render::PixelFormat::Rgba8888));
                content->notifyDamage(base::Rect(10, 30, 10, 10));
            }
            const auto damage = tracker.update(*scene.root(), context.logicalViewport, scale);
            const base::Rect physical = render::physicalDamage(damage, context, size);
            const base::Rect repaint = render::coverSamples(physical, tracker.samples(kept),
                                                       context, size);
            render::EglRendererTestAccess::applyRepaint(*renderer, repaint);
            {
                const render::BackdropPass backdrops(*renderer, scope,
                                                     tracker.freshBackdrops(kept));
                renderer->clear(scene.background());
                scene.root()->render(*renderer);
            }
            ui::Bitmap partial(size, ui::Bitmap::Format::Rgba8888Premultiplied);
            glReadPixels(0, 0, 400, 400, GL_RGBA, GL_UNSIGNED_BYTE, partial.bits());
            partial = partial.mirrored(false, true).converted(ui::Bitmap::Format::Argb32Premultiplied);
            const auto complete = renderer->captureFrame([&] {
                scene.render(*renderer);
            });
            int worst = 0;
            for (int y = 0; y < size.height(); ++y) {
                for (int x = 0; x < size.width(); ++x) {
                    const uint32_t p = partial.pixel(x, y), c = complete.pixel(x, y);
                    for (int shift = 0; shift < 32; shift += 8)
                        worst = std::max(worst, std::abs(int(p >> shift & 0xff) - int(c >> shift & 0xff)));
                }
            }
            EXPECT_EQ(worst, 0) << "scale=" << scale << " frame=" << frame;
        }
        renderer->cancelFrame();
    }
    renderer->releaseTarget(target.get());
}

// A window keeps a texture of itself for a close or a minimize to start from.
// A blur in it reads the output behind the window, which outside a partial
// repaint still shows the window itself.
TEST(DamagePixels, AWindowSnapshotNeverBlursWhatAPartialRepaintLeftBehind)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const base::Size size(400, 400);
    const EGLint attributes[] = {EGL_WIDTH, 400, EGL_HEIGHT, 400, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, size);
    const render::OutputRenderContext context {"test", base::Rect(base::Point(), size), size, 1};
    const auto snapshot = [&](bool partial) {
        scene::Scene scene;
        scene.setSize(size);
        auto *strip = scene.root()->addChild(
                std::make_unique<scene::ColorNode>(render::Color {1, 1, 1, 1}));
        strip->setGeometry({170, 60, 20, 240});
        auto *window = static_cast<scene::WindowAnimation *>(
                scene.root()->addChild(std::make_unique<scene::WindowAnimation>()));
        window->setGeometry({120, 120, 120, 120});
        window->finish();
        auto *content = static_cast<scene::SurfaceNode *>(
                window->addChild(std::make_unique<scene::SurfaceNode>()));
        content->setGeometry({0, 0, 120, 120});
        content->setBackgroundBlur({0, 0, 120, 120}, 12);
        // Far from all the blur reads.
        auto *moving = scene.root()->addChild(
                std::make_unique<scene::ColorNode>(render::Color {1, 0, 0, 1}));
        renderer->setOutputContext(context);
        ui::Bitmap image;
        if (!renderer->beginFrame(target.get()))
            return image;
        const std::vector<uint8_t> pixels(120 * 120 * 4, 96);
        content->setTexture(renderer->createTexture({120, 120}, pixels.data()));
        scene::DamageTracker tracker;
        const auto frame = [&] {
            const auto damage = tracker.update(*scene.root(), context.logicalViewport);
            if (partial) {
                render::EglRendererTestAccess::applyRepaint(
                        *renderer,
                        render::coverSamples(render::physicalDamage(damage, context, size),
                                             tracker.samples(), context, size));
            }
            renderer->clear(scene.background());
            scene.root()->render(*renderer);
        };
        for (int step = 0; step < 4; ++step) {
            moving->setGeometry({4 + step, 4, 10, 10});
            frame();
        }
        window->setVisible(false);
        window->start(scene::WindowAnimation::Type::Minimize, window->geometry(),
                      {300, 380, 20, 20});
        window->requestSnapshot(
                [&](const ui::Bitmap &preview) {
                    image = preview;
                },
                1000);
        frame();
        renderer->cancelFrame();
        return image;
    };
    const ui::Bitmap partial = snapshot(true);
    const ui::Bitmap complete = snapshot(false);
    ASSERT_FALSE(complete.isNull());
    EXPECT_EQ(partial, complete);
    renderer->releaseTarget(target.get());
}

// A capture between output frames may draw a window's changed content first;
// the output frame after it then reuses that snapshot.
TEST(DamagePixels, AWindowSnapshotDrawnByACaptureServesTheNextFrame)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const base::Size size(200, 200);
    const EGLint attributes[] = {EGL_WIDTH, 200, EGL_HEIGHT, 200, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, size);
    const render::OutputRenderContext context {"test", base::Rect(base::Point(), size), size, 1};
    scene::Scene scene;
    scene.setSize(size);
    auto *window = static_cast<scene::WindowAnimation *>(
            scene.root()->addChild(std::make_unique<scene::WindowAnimation>()));
    window->setGeometry({40, 40, 100, 80});
    window->finish();
    auto *content = static_cast<scene::SurfaceNode *>(
            window->addChild(std::make_unique<scene::SurfaceNode>()));
    content->setGeometry({0, 0, 100, 80});
    renderer->setOutputContext(context);
    ASSERT_TRUE(renderer->beginFrame(target.get()));
    std::vector<uint8_t> pixels(100 * 80 * 4, 255);
    std::shared_ptr<render::Texture> texture = renderer->createTexture({100, 80}, pixels.data());
    content->setTexture(texture);
    scene::DamageTracker tracker;
    for (int frame = 0; frame < 6; ++frame) {
        const int x = 10 + frame * 7;
        for (int y = 20; y < 30; ++y) {
            pixels[(y * 100 + x) * 4] = 0;
            pixels[(y * 100 + x) * 4 + 1] = 0;
        }
        ASSERT_TRUE(renderer->updateTexture(texture.get(), pixels.data(), 400,
                                            render::PixelFormat::Rgba8888));
        content->notifyDamage(base::Rect(x, 20, 1, 10));
        const auto captured = renderer->captureFrame([&] {
            scene.render(*renderer);
        });
        const auto damage = tracker.update(*scene.root(), context.logicalViewport);
        render::EglRendererTestAccess::applyRepaint(
                *renderer, render::physicalDamage(damage, context, size));
        renderer->clear(scene.background());
        scene.root()->render(*renderer);
        ui::Bitmap partial(size, ui::Bitmap::Format::Rgba8888Premultiplied);
        glReadPixels(0, 0, 200, 200, GL_RGBA, GL_UNSIGNED_BYTE, partial.bits());
        partial = partial.mirrored(false, true).converted(ui::Bitmap::Format::Argb32Premultiplied);
        EXPECT_EQ(partial, captured) << "frame=" << frame;
        EXPECT_EQ(partial.pixel(40 + x, 40 + 25), 0xff0000ffu) << "frame=" << frame;
    }
    renderer->cancelFrame();
    renderer->releaseTarget(target.get());
}

// The second blur stage reads what the children placed below a surface drew:
// their edges come out soft under it, and nowhere else.
TEST(DamagePixels, ABlurOverChildrenBelowBlursThem)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const base::Size size(400, 400);
    const EGLint attributes[] = {EGL_WIDTH, 400, EGL_HEIGHT, 400, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, size);
    scene::Scene scene;
    scene.setSize(size);
    auto *parent = static_cast<scene::SurfaceNode *>(
            scene.root()->addChild(std::make_unique<scene::SurfaceNode>()));
    parent->setGeometry({100, 100, 200, 160});
    auto *below = parent->addChild(std::make_unique<scene::ColorNode>(render::Color {1, 1, 1, 1}));
    below->setGeometry({90, 0, 20, 160});
    parent->setChildrenBelowParent(1);
    const render::OutputRenderContext context {"test", base::Rect(base::Point(), size), size, 1};
    renderer->setOutputContext(context);
    ASSERT_TRUE(renderer->beginFrame(target.get()));
    std::vector<uint8_t> pixels(200 * 160 * 4, 0);
    parent->setTexture(renderer->createTexture({200, 160}, pixels.data()));
    const auto frame = [&] {
        return renderer->captureFrame([&] {
            scene.render(*renderer);
        });
    };
    const auto red = [](uint32_t pixel) { return int(pixel >> 16 & 0xff); };
    const ui::Bitmap sharp = frame();
    EXPECT_EQ(red(sharp.pixel(186, 180)), 0);
    EXPECT_EQ(red(sharp.pixel(192, 180)), 255);
    // Over the lower half only.
    parent->setSubsurfaceBlur({0, 80, 200, 80}, 12);
    const ui::Bitmap blurred = frame();
    EXPECT_GT(red(blurred.pixel(186, 220)), 20);
    EXPECT_LT(red(blurred.pixel(192, 220)), 250);
    EXPECT_EQ(blurred.pixel(186, 130), sharp.pixel(186, 130));
    EXPECT_EQ(blurred.pixel(192, 130), sharp.pixel(192, 130));
    parent->setSubsurfaceBlur({}, 0);
    EXPECT_EQ(frame(), sharp);
    renderer->cancelFrame();
    renderer->releaseTarget(target.get());
}

// Partial frames, a child below changing under the blur and something above
// it moving, kept results and all, match frames drawn whole.
TEST(DamagePixels, ABlurOverChildrenBelowRedoneInPartMatchesTheCompleteFrame)
{
    auto renderer = render::EglRendererTestAccess::create();
    ASSERT_TRUE(renderer);
    const base::Size size(400, 400);
    const EGLint attributes[] = {EGL_WIDTH, 400, EGL_HEIGHT, 400, EGL_NONE};
    const auto surface =
            eglCreatePbufferSurface(renderer->eglDisplay(), renderer->eglConfig(), attributes);
    ASSERT_NE(surface, EGL_NO_SURFACE);
    auto target = std::make_unique<render::EglRenderTarget>(renderer->eglDisplay(), surface,
                                                            nullptr, size);
    for (double scale : {1.0, 1.5}) {
        scene::Scene scene;
        scene.setSize(size);
        auto *backdrop = scene.root()->addChild(
                std::make_unique<scene::ColorNode>(render::Color {0, 0, 1, 1}));
        backdrop->setGeometry({60, 60, 30, 300});
        auto *parent = static_cast<scene::SurfaceNode *>(
                scene.root()->addChild(std::make_unique<scene::SurfaceNode>()));
        parent->setGeometry({50, 50, 300, 300});
        auto *below = parent->addChild(std::make_unique<scene::ColorNode>(render::Color {1, 1, 1, 1}));
        parent->setChildrenBelowParent(1);
        parent->setSubsurfaceBlur({40, 40, 200, 120}, 12);
        auto *above = scene.root()->addChild(
                std::make_unique<scene::ColorNode>(render::Color {1, 0, 0, 1}));
        const render::OutputRenderContext context {"test", base::Rect(base::Point(), size), size, scale};
        renderer->setOutputContext(context);
        ASSERT_TRUE(renderer->beginFrame(target.get()));
        std::vector<uint8_t> pixels(300 * 300 * 4, 0);
        parent->setTexture(renderer->createTexture({300, 300}, pixels.data()));
        scene::DamageTracker tracker;
        const auto scope = uint64_t(reinterpret_cast<uintptr_t>(&tracker));
        const auto kept = [&](uint64_t owner) {
            return renderer->keepsBackdrop(scope, owner);
        };
        for (int frame = 0; frame < 6; ++frame) {
            // What the child below shows changes on the second and fifth frames.
            below->setGeometry({frame >= 4 ? 120 : frame >= 1 ? 100 : 80, 20, 16, 260});
            above->setGeometry({150 + frame * 7, 260, 8, 8});
            const auto damage = tracker.update(*scene.root(), context.logicalViewport, scale);
            const base::Rect physical = render::physicalDamage(damage, context, size);
            const base::Rect repaint = render::coverSamples(physical, tracker.samples(kept), context, size);
            // At 1.5 the blur and its halo reach over the whole output.
            if (frame && scale == 1)
                EXPECT_LT(repaint.width() * repaint.height(), size.width() * size.height()) << "frame=" << frame;
            render::EglRendererTestAccess::applyRepaint(*renderer, repaint);
            {
                const render::BackdropPass backdrops(*renderer, scope, tracker.freshBackdrops(kept));
                renderer->clear(scene.background());
                scene.root()->render(*renderer);
            }
            ui::Bitmap partial(size, ui::Bitmap::Format::Rgba8888Premultiplied);
            glReadPixels(0, 0, 400, 400, GL_RGBA, GL_UNSIGNED_BYTE, partial.bits());
            partial = partial.mirrored(false, true).converted(ui::Bitmap::Format::Argb32Premultiplied);
            const auto complete = renderer->captureFrame([&] {
                scene.render(*renderer);
            });
            EXPECT_EQ(partial, complete) << "scale=" << scale << " frame=" << frame;
        }
        renderer->cancelFrame();
    }
    renderer->releaseTarget(target.get());
}
