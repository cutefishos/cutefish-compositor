// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Adapted from chromium/src/components/exo/buffer_unittest.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include <gtest/gtest.h>
#include <list>

#include "compositor/buffer.h"
#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "fake_renderer.h"
#include "scene/scene.h"
#include "scene/surface_node.h"
#include "shm_test_util.h"

using namespace cutefish;
using namespace cutefish::compositor;
using cutefish::testsupport::makePool;

namespace {

class RetainingBuffer : public Buffer
{
public:
    cutefish::base::Size size() const override { return {8, 8}; }

    render::PixelFormat format() const override { return render::PixelFormat::Bgra8888; }

    const uint8_t *pixels() const override { return nullptr; }

    int stride() const override { return 0; }

    bool retainsClientStorage() const override { return true; }
};

class BufferTest : public testing::Test
{
protected:
    std::shared_ptr<ShmBuffer> makeBuffer(const base::Size &size = base::Size(16, 16))
    {
        return m_pool->createBuffer(size, 0, size.width() * 4,
                                    render::PixelFormat::Bgra8888,
                                    nullptr);
    }

    Surface *makeSurface()
    {
        m_surfaces.push_back(m_compositor.createSurface());
        return m_surfaces.back().get();
    }

    int &counter() { return m_releaseCounts.emplace_back(0); }

    // Surface destruction can release a retained attachment after TestBody returns.
    std::list<int> m_releaseCounts;
    scene::Scene m_scene;
    Compositor m_compositor {m_scene.root()};
    std::vector<std::unique_ptr<Surface>> m_surfaces;
    std::shared_ptr<ShmPool> m_pool = makePool(1024 * 1024);
    cutefish::testsupport::FakeRenderer m_renderer;
};

TEST_F(BufferTest, ANewBufferOwesNoRelease)
{
    std::shared_ptr<ShmBuffer> buffer = makeBuffer();
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0u, buffer->attachCount());
    EXPECT_TRUE(buffer->isReleased());
    EXPECT_FALSE(buffer->contentsInUse());
}

TEST_F(BufferTest, CommitAloneDoesNotRelease)
{
    std::shared_ptr<ShmBuffer> buffer = makeBuffer();
    int &releases = counter();
    buffer->setReleaseCallback([&releases] {
        ++releases;
    });

    Surface *surface = makeSurface();
    surface->attach(buffer, base::Point());
    surface->commit();

    EXPECT_EQ(1u, buffer->attachCount());
    EXPECT_TRUE(buffer->contentsInUse());
    EXPECT_EQ(0, releases);
}

TEST_F(BufferTest, ShmContentsAreReleasedOnceUploaded)
{
    std::shared_ptr<ShmBuffer> buffer = makeBuffer();
    int &releases = counter();
    buffer->setReleaseCallback([&releases] {
        ++releases;
    });

    Surface *surface = makeSurface();
    surface->attach(buffer, base::Point());
    surface->commit();
    ASSERT_EQ(0, releases);

    EXPECT_TRUE(surface->updateTexture(m_renderer));
    EXPECT_EQ(1, releases);
    EXPECT_EQ(1u, buffer->attachCount());
    EXPECT_TRUE(buffer->isReleased());

    surface->updateTexture(m_renderer);
    EXPECT_EQ(1, releases);
}

TEST_F(BufferTest, RetainedContentsWaitForDetach)
{
    RetainingBuffer raw;
    int &releases = counter();
    raw.setReleaseCallback([&releases] {
        ++releases;
    });

    raw.onAttach();
    raw.onContentsConsumed();
    EXPECT_EQ(0, releases) << "still the committed content of a surface";

    raw.onDetach();
    EXPECT_EQ(1, releases);
}

TEST_F(BufferTest, ReplacementDetachesTheOldBuffer)
{
    std::shared_ptr<ShmBuffer> first = makeBuffer();
    std::shared_ptr<ShmBuffer> second = makeBuffer();
    int &firstReleases = counter();
    int &secondReleases = counter();
    first->setReleaseCallback([&firstReleases] {
        ++firstReleases;
    });
    second->setReleaseCallback([&secondReleases] {
        ++secondReleases;
    });

    Surface *surface = makeSurface();
    surface->attach(first, base::Point());
    surface->commit();
    surface->attach(second, base::Point());
    surface->commit();

    EXPECT_EQ(0u, first->attachCount());
    EXPECT_EQ(1u, second->attachCount());
    EXPECT_EQ(1, firstReleases);
    EXPECT_EQ(0, secondReleases);
}

TEST_F(BufferTest, ReCommittingTheSameBufferOwesAnotherRelease)
{
    std::shared_ptr<ShmBuffer> buffer = makeBuffer();
    int &releases = counter();
    buffer->setReleaseCallback([&releases] {
        ++releases;
    });

    Surface *surface = makeSurface();
    surface->attach(buffer, base::Point());
    surface->commit();
    surface->updateTexture(m_renderer);
    ASSERT_EQ(1, releases);

    surface->attach(buffer, base::Point());
    surface->commit();
    EXPECT_FALSE(buffer->isReleased());
    EXPECT_EQ(1u, buffer->attachCount()) << "attach runs before detach, so it never hits zero";

    surface->updateTexture(m_renderer);
    EXPECT_EQ(2, releases);
}

TEST_F(BufferTest, TheSameBufferOnTwoSurfaces)
{
    RetainingBuffer raw;
    int &releases = counter();
    raw.setReleaseCallback([&releases] {
        ++releases;
    });

    raw.onAttach();
    raw.onAttach();
    raw.onContentsConsumed();
    raw.onDetach();
    EXPECT_EQ(0, releases);
    raw.onDetach();
    EXPECT_EQ(1, releases);
}

TEST_F(BufferTest, DestroyingTheBufferLeavesTheSurfaceUsable)
{
    std::shared_ptr<ShmBuffer> buffer = makeBuffer(base::Size(32, 24));
    Surface *surface = makeSurface();
    surface->attach(buffer, base::Point());
    surface->commit();
    ASSERT_TRUE(surface->updateTexture(m_renderer));

    buffer.reset();

    EXPECT_EQ(nullptr, surface->buffer());
    EXPECT_EQ(base::Size(32, 24), surface->bufferSize());
    EXPECT_EQ(base::Size(32, 24), surface->contentSize());
    ASSERT_NE(nullptr, surface->node());
    EXPECT_NE(nullptr, surface->node()->texture());

    surface->commit();
    EXPECT_FALSE(surface->updateTexture(m_renderer));
}

TEST_F(BufferTest, DestroyingTheSurfaceDetaches)
{
    std::shared_ptr<ShmBuffer> buffer = makeBuffer();
    int &releases = counter();
    buffer->setReleaseCallback([&releases] {
        ++releases;
    });

    Surface *surface = makeSurface();
    surface->attach(buffer, base::Point());
    surface->commit();

    m_surfaces.clear();
    EXPECT_EQ(0u, buffer->attachCount());
    EXPECT_EQ(1, releases);
}

} // namespace
