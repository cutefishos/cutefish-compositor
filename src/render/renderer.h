// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RENDER_RENDERER_H_
#define CUTEFISH_RENDER_RENDERER_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


#include "render/dmabuf.h"
#include "render/glass.h"
#include "render/output_render_context.h"
#include "render/presentation.h"
#include "render/rounded_shape.h"
#include "render/sync_fence.h"
#include "render/texture.h"
#include "base/region.h"
#include "base/transform.h"
#include "ui/bitmap.h"
#include "ui/path.h"

namespace cutefish::render {

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    friend bool operator==(const Color &, const Color &) = default;
};

// In the pixels of the image it is drawn into.
struct ShadowLobe {
    float sigma = 0;
    base::Point offset; // Positive down/right.
    float opacity = 0;
};

// The material's fill, tint and lighting still apply.
std::optional<Color> opaqueBackdrop();
void setOpaqueBackdrop(std::optional<Color> color);

inline bool usesOpaqueBackdrop(const GlassParams &glass)
{
    return !glass.ignoresOpaqueBackdrop && opaqueBackdrop().has_value();
}

// A non-empty `changed` is the logical rectangle of its backdrop that changed
// since.
struct KeptBackdrop {
    uint64_t owner = 0;
    base::Rect changed;

    friend bool operator==(const KeptBackdrop &, const KeptBackdrop &) = default;
};

// Opaque to everything except the renderer that created it.
class RenderTarget
{
public:
    virtual ~RenderTarget() = default;
    virtual base::Size size() const = 0;
};

// Draw geometry is logical; rasterScale maps it into the physical target.

class PendingImage
{
public:
    virtual ~PendingImage() = default;
    // Premultiplied ARGB32, rows top-down; null if the read failed.
    virtual ui::Bitmap take() = 0;
};
class Renderer
{
public:
    virtual ~Renderer() = default;

    PresentationSubmission *presentationSubmission = nullptr;

    void recordPresentation(const std::shared_ptr<PresentationContent> &content, const base::Rect &rect)
    {
        base::Rect viewport = m_outputContext.logicalViewport;
        if (viewport.isEmpty()) {
            const base::Size logical(std::ceil(viewportSize().width() / rasterScale()),
                                std::ceil(viewportSize().height() / rasterScale()));
            viewport = base::Rect(base::Point(), logical);
        }
        if (presentationSubmission && rect.intersects(viewport))
            presentationSubmission->add(content, false);
    }

    double rasterScale() const { return m_rasterScale; }

    void setRasterScale(double scale) { m_rasterScale = scale; }

    virtual void setOutputContext(OutputRenderContext context)
    {
        m_outputContext = std::move(context);
        m_rasterScale = m_outputContext.scale;
    }

    const OutputRenderContext &outputContext() const { return m_outputContext; }

    base::PointF pixelAligned(const base::PointF &logical) const
    {
        const base::PointF outputOrigin = m_outputContext.logicalViewport.topLeft();
        const base::PointF origin = (logical - outputOrigin) * m_rasterScale;
        return outputOrigin + base::PointF(std::round(origin.x()), std::round(origin.y())) / m_rasterScale;
    }

    // An edge that falls inside a pixel is the client's antialiasing, so that
    // pixel is left out.
    base::RectF pixelsInside(const base::RectF &logical) const
    {
        constexpr double kEpsilon = 0.01;
        const base::PointF outputOrigin = m_outputContext.logicalViewport.topLeft();
        const base::PointF start = (logical.topLeft() - outputOrigin) * m_rasterScale;
        const base::PointF end = (logical.bottomRight() - outputOrigin) * m_rasterScale;
        const base::PointF first(std::ceil(start.x() - kEpsilon), std::ceil(start.y() - kEpsilon));
        const base::PointF last(std::max(first.x(), std::floor(end.x() + kEpsilon)),
                           std::max(first.y(), std::floor(end.y() + kEpsilon)));
        const base::PointF topLeft = outputOrigin + first / m_rasterScale;
        const base::PointF bottomRight = outputOrigin + last / m_rasterScale;
        return base::RectF::fromEdges(topLeft.x(), topLeft.y(), bottomRight.x(), bottomRight.y());
    }

    base::RectF pixelsAround(const base::RectF &logical) const
    {
        constexpr double kEpsilon = 0.01;
        const base::PointF outputOrigin = m_outputContext.logicalViewport.topLeft();
        const base::PointF start = (logical.topLeft() - outputOrigin) * m_rasterScale;
        const base::PointF end = (logical.bottomRight() - outputOrigin) * m_rasterScale;
        const base::PointF first(std::floor(start.x() + kEpsilon), std::floor(start.y() + kEpsilon));
        const base::PointF last(std::ceil(end.x() - kEpsilon), std::ceil(end.y() - kEpsilon));
        const base::PointF topLeft = outputOrigin + first / m_rasterScale;
        const base::PointF bottomRight = outputOrigin + last / m_rasterScale;
        return base::RectF::fromEdges(topLeft.x(), topLeft.y(), bottomRight.x(), bottomRight.y());
    }

    // Every draw call must sit between beginFrame() and endFrame().
    virtual bool beginFrame(RenderTarget *target) = 0;
    virtual bool endFrame() = 0;

    // `samples` are the logical regions backdrop blurs read, which a repaint
    // reaching them covers whole.
    virtual void prepareDamage(const base::Region &, const std::vector<base::Rect> &) { }

    // Everywhere else the target keeps what the last frame left there.
    virtual bool repaints(const base::Rect &) const { return true; }

    // The repaint of an output frame or the bounds of a pass; none culls
    // nothing.
    std::optional<base::Rect> paintClip;

    // False when a draw inside `logical` would be clipped away whole.
    bool drawsInto(const base::Rect &logical) const
    {
        return !paintClip || paintClip->intersects(logical);
    }

    // The previous composited buffer must be left untouched and reusable.
    virtual void cancelFrame() { }

    // Animation snapshots retain the body; the physical-width outline is drawn afterward.
    bool suppressWindowOutlines = false;

    // A window drawn scaled from its snapshot is not where its blurs would read
    // the screen.
    bool suppressBackgroundBlur = false;

    // Its surfaces hidden along with it are drawn too.
    bool revealHiddenSurfaces = false;

    // Multiplies the opacity of every surface and colour drawn.
    float opacityScale = 1;
    // An opaque texture drawn at an opacity below 1 then replaces the pixels
    // under it, scaled, instead of blending.
    bool overwriteOpaque = false;

    // The window's visual node clips its content with these instead of its own.
    struct WindowCorners {
        double radius = 0;
        base::SizeF radiusScale {1, 1};
    };
    std::optional<WindowCorners> windowCorners;
    bool capturing = false;

    // Milliseconds of the steady clock, -1 outside an output frame; fractional,
    // as whole milliseconds would step a 13.3 ms period unevenly.
    double frameTime = -1;
    // Milliseconds; 0 outside an output frame.
    double framePeriod = 0;

    virtual void pushRoundedClip(const RoundedShape &shape) { m_roundedClips.push_back(shape); }

    virtual void popRoundedClip()
    {
        if (!m_roundedClips.empty())
            m_roundedClips.pop_back();
    }

    virtual void drawRoundedOutline(const RoundedShape &, const Color &, double) { }

    virtual void clear(const Color &color) = 0;
    virtual void fillRect(const base::Rect &rect, const Color &color) = 0;

    virtual void fillRectF(const base::RectF &rect, const Color &color)
    {
        fillRect(rect.toEnclosingRect(), color);
    }

    virtual void drawTextureF(Texture *texture, const base::RectF &target, float alpha = 1.0f)
    {
        drawTexture(texture, target.toEnclosingRect(), alpha);
    }

    virtual void drawTexture(Texture *texture, const base::Rect &target, float alpha = 1.0f) = 0;

    // `transform` maps logical coordinates, so `target` can be rotated or scaled.
    virtual void drawTextureTransformed(Texture *texture, const base::RectF &target,
                                        const base::AffineTransform &transform, float alpha = 1.0f)
    {
        drawTextureF(texture, transform.mapRect(target), alpha);
    }

    virtual void drawTextureMapped(Texture *texture, const base::Rect &target, float alpha,
                                   const base::AffineTransform &mapping) = 0;

    struct MappedRect {
        base::RectF target;
        base::AffineTransform mapping;
    };

    virtual void drawTextureMappedRects(Texture *texture, const std::vector<MappedRect> &rects,
                                        float alpha)
    {
        for (const MappedRect &rect : rects)
            drawTextureMapped(texture, rect.target.toEnclosingRect(), alpha, rect.mapping);
    }

    // A non-empty `shape`, in the coordinates of `bounds`, masks the blur
    // instead of the rounded rectangle.
    virtual void drawBackgroundBlur(const base::Rect &bounds, int radius, const base::Rect &clip,
                                    const GlassParams &glass = {}, const ui::Path &shape = {},
                                    uint64_t owner = 0)
    {
    }

    virtual bool keepsBackdrop(uint64_t /*scope*/, uint64_t /*owner*/) const { return false; }
    // Empty when none; nothing for a blur drawn from scratch.
    virtual std::optional<base::Rect> keptBackdropChange(uint64_t /*scope*/, uint64_t /*owner*/) const
    {
        return std::nullopt;
    }
    virtual void beginBackdropPass(uint64_t /*scope*/, const std::vector<KeptBackdrop> & /*fresh*/) { }
    // Zero outside a BackdropPass, and in the passes nested in one.
    uint64_t backdropScope = 0;

    // The base implementation blocks, which is never wrong, only slower.
    virtual void waitFence(const SyncFence &fence) { fence.wait(kFenceWaitMs); }

    // An invalid fence means the caller must release buffers on frame
    // completion instead.
    virtual SyncFence createReleaseFence() { return SyncFence(); }

    // A wedged client must not stall the compositor forever.
    static constexpr int kFenceWaitMs = 500;

    // Uploads `size` pixels starting at `pixels`, `stride` bytes per row.
    virtual std::unique_ptr<Texture> createTexture(const base::Size &size, const uint8_t *pixels,
                                                   int stride, PixelFormat format) = 0;

    // False when `texture` was not created by this renderer or its format or
    // stride no longer fit; the caller then creates a new one.
    virtual bool updateTexture(Texture *texture, const uint8_t *pixels, int stride,
                               PixelFormat format) = 0;

    // `damage` is in buffer pixels; everywhere else the texture keeps what it
    // holds.
    virtual bool updateTexture(Texture *texture, const uint8_t *pixels, int stride,
                               PixelFormat format, const base::Region &)
    {
        return updateTexture(texture, pixels, stride, format);
    }

    // Tightly packed RGBA8888, `size.width() * size.height() * 4` bytes.
    std::unique_ptr<Texture> createTexture(const base::Size &size, const uint8_t *rgba)
    {
        return createTexture(size, rgba, size.width() * 4, PixelFormat::Rgba8888);
    }

    // Only inside a frame; the texture owns its pixels independently of client
    // resources.
    virtual bool renderToTexture(std::unique_ptr<Texture> &texture, const base::Rect &bounds,
                                 const std::function<void()> &paint)
    {
        return false;
    }

    // An empty `repaint`, or a texture of another size or scale, draws
    // everything.
    virtual bool renderToTexture(std::unique_ptr<Texture> &texture, const base::Rect &bounds,
                                 const base::Rect &repaint, const std::function<void()> &paint)
    {
        return renderToTexture(texture, bounds, paint);
    }

    // The caster is painted with no clip, one logical unit to an image pixel;
    // only inside a frame, null where unsupported.
    virtual std::unique_ptr<Texture> renderShadow(const base::Size &size,
                                                  const std::vector<ShadowLobe> &lobes,
                                                  const std::function<void()> &paintCaster)
    {
        return nullptr;
    }

    // Until the next renderToTexture() into it.
    virtual void enableTrilinearFiltering(Texture *texture) { }

    // A negative level lifts the limit.
    virtual void limitMipmapLevel(Texture *texture, int level) { }

    // Capture only inside a frame; the display render target remains untouched.
    virtual ui::Bitmap captureFrame(const std::function<void()> &paint) { return {}; }

    // Only inside a frame; premultiplied ARGB32, rows top-down.
    virtual ui::Bitmap captureImage(const OutputRenderContext &context, const base::Size &size,
                                    const std::function<void()> &paint)
    {
        return {};
    }

    // Null where only the waiting path exists.
    virtual std::shared_ptr<PendingImage> captureFrameLater(const std::function<void()> &paint)
    {
        return nullptr;
    }

    virtual std::shared_ptr<PendingImage> captureImageLater(const OutputRenderContext &context,
                                                            const base::Size &size,
                                                            const std::function<void()> &paint)
    {
        return nullptr;
    }

    // `attributes` must outlive the call; false when the buffer cannot be a
    // render target here.
    virtual bool captureToDmaBuf(const OutputRenderContext &context, const base::Size &size,
                                 const DmaBufAttributes &attributes,
                                 const std::function<void()> &paint)
    {
        return false;
    }

    // Empty without support; the render node is 0 when unknown.
    virtual const DmaBufFormats &captureDmaBufFormats() const
    {
        static const DmaBufFormats none;
        return none;
    }

    virtual dev_t captureDmaBufDevice() const { return 0; }

    // False when it cannot.
    virtual bool copyOutputFrame(const base::Size &size) { return false; }

    // The image's logical coordinates are its pixels; only inside a frame,
    // premultiplied ARGB32.
    virtual ui::Bitmap renderToImage(const base::Size &size, const std::function<void()> &paint)
    {
        return {};
    }

    virtual base::Size viewportSize() const = 0;

    // Call only with the context current; returned references last until the
    // next cache lookup.
    using TextureSet = std::vector<std::unique_ptr<Texture>>;
    const TextureSet &cachedTextureSet(const std::string &key, const std::function<TextureSet()> &make);

    // Derived GPU renderers clear these before destroying their context.
    void clearTextureCache() { m_textureCache.clear(); }

    size_t textureCacheSize() const { return m_textureCache.size(); }

    uint64_t textureCacheMisses() const { return m_textureCacheMisses; }

    static constexpr size_t textureCacheLimit = 16;

protected:
    std::vector<RoundedShape> m_roundedClips;

private:
    double m_rasterScale = 1;
    OutputRenderContext m_outputContext;

    struct CachedTextures {
        std::string key;
        TextureSet textures;
    };

    std::list<CachedTextures> m_textureCache;
    uint64_t m_textureCacheMisses = 0;
};

// The blurs of `fresh` draw what they kept, redone where their backdrop
// changed; the others' results are dropped.
class BackdropPass
{
public:
    BackdropPass(Renderer &renderer, uint64_t scope, const std::vector<KeptBackdrop> &fresh)
        : m_renderer(renderer)
        , m_saved(std::exchange(renderer.backdropScope, scope))
    {
        renderer.beginBackdropPass(scope, fresh);
    }

    ~BackdropPass() { m_renderer.backdropScope = m_saved; }

    BackdropPass(const BackdropPass &) = delete;
    BackdropPass &operator=(const BackdropPass &) = delete;

private:
    Renderer &m_renderer;
    uint64_t m_saved;
};

} // namespace cutefish::render

#endif // CUTEFISH_RENDER_RENDERER_H_
