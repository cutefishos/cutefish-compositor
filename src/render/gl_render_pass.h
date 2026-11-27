// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "render/gl_texture.h"
#include "render/renderer.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "base/geometry.h"

namespace cutefish::render {

template <typename T> class ScopedValue
{
public:
    explicit ScopedValue(T &slot)
        : m_slot(slot)
        , m_saved(slot)
    {
    }

    ~ScopedValue() { m_slot = std::move(m_saved); }

    ScopedValue(const ScopedValue &) = delete;
    ScopedValue &operator=(const ScopedValue &) = delete;

private:
    T &m_slot;
    T m_saved;
};

struct BackdropSource {
    unsigned int framebuffer;
    base::Size size;
    base::PointF offset;
    // An output's transform, 0 for an offscreen pass.
    uint32_t transform = 0;
    // Attached texture for an offscreen render pass; null for the default framebuffer.
    Texture *texture = nullptr;

    base::Size oriented() const { return transform % 2 ? size.transposed() : size; }

    // GL window coordinates of the framebuffer, bottom up.
    base::Rect framebufferRect(const base::Rect &rect) const;
};

class GlRenderPass
{
public:
    explicit GlRenderPass(std::shared_ptr<EglContext> context)
        : m_context(std::move(context))
    {
    }

    // Copy replaces every covered pixel, for a buffer of unknown previous
    // contents.
    enum class BlendMode {
        SourceOver,
        Copy,
    };

    const base::Size &viewport() const { return m_viewport; }

    const base::PointF &offset() const { return m_offset; }

    BlendMode blendMode() const { return m_blendMode; }

    void setGeometry(const base::Size &viewport, const base::PointF &offset)
    {
        m_viewport = viewport;
        m_offset = offset;
    }

    // Outermost first; `transform` is the current pass's own.
    std::vector<BackdropSource> backdropSources(double scale, uint32_t transform) const;
    // The context must be current.
    void releaseResources();
    ui::Bitmap capture(Renderer &renderer, const base::Size &size, const std::function<void()> &paint);
    ui::Bitmap renderToImage(Renderer &renderer, const base::Size &size, const std::function<void()> &paint);
    bool render(Renderer &renderer, std::unique_ptr<Texture> &texture, const base::Rect &bounds,
                const std::function<void()> &paint, const base::Rect &repaint = base::Rect());
    // The next render() into it drops them.
    void generateMipmaps(Texture *texture);
    void limitMipmapLevel(Texture *texture, int level);

    // The logical viewport and scale come from `context`, so a window can be
    // painted on its own.
    ui::Bitmap captureImage(Renderer &renderer, const OutputRenderContext &context, const base::Size &size,
                        const std::function<void()> &paint);
    // Collected by take(); null without GLES 3.
    std::shared_ptr<PendingImage> readBack(Renderer &renderer, const OutputRenderContext &context,
                                           const base::Size &size, const std::function<void()> &paint);
    // Ends up top-down in the DMA-BUF behind `image`; false if the image cannot
    // be rendered into.
    bool captureToImage(Renderer &renderer, const OutputRenderContext &context, const base::Size &size,
                        EGLImageKHR image, const std::function<void()> &paint);

private:
    std::unique_ptr<GlTexture> captureTexture(Renderer &renderer,
                                              const OutputRenderContext &context,
                                              const base::Size &size,
                                              const std::function<void()> &paint);
    int maxTextureSize();

    std::shared_ptr<EglContext> m_context;
    int m_maxTextureSize = 0;
    base::Size m_viewport;
    base::PointF m_offset;
    BlendMode m_blendMode = BlendMode::SourceOver;
    Texture *m_targetTexture = nullptr;
    std::vector<BackdropSource> m_sources;
    std::vector<unsigned int> m_framebuffers;
};

} // namespace cutefish::render
