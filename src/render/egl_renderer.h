// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RENDER_EGL_RENDERER_H_
#define CUTEFISH_RENDER_EGL_RENDERER_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <array>
#include <chrono>
#include <initializer_list>
#include <memory>
#include <optional>
#include <vector>

#include "render/backdrop_filter.h"
#include "render/color_transform.h"
#include "render/dmabuf.h"
#include "render/drop_shadow.h"
#include "render/egl_context.h"
#include "render/egl_dmabuf_importer.h"
#include "render/egl_fence.h"
#include "render/egl_render_target.h"
#include "render/gl_program.h"
#include "render/gl_render_pass.h"
#include "render/gpu_timer.h"
#include "render/renderer.h"
#include "base/region.h"
#include "base/timer.h"
#include "base/transform.h"

struct gbm_device;
struct gbm_surface;

namespace cutefish::render {

// DRM/KMS lives entirely outside it.
class EglRenderer : public Renderer, public DmaBufDevice
{
public:
    ~EglRenderer() override;

    // `format` is the DRM fourcc of the GBM surfaces; the EGL config is matched
    // through EGL_NATIVE_VISUAL_ID.
    static std::unique_ptr<EglRenderer> create(gbm_device *device, uint32_t format);

    std::unique_ptr<EglRenderTarget> createTarget(gbm_surface *surface, const base::Size &size);

    // Must be called before an output destroys its target.
    void releaseTarget(EglRenderTarget *target);

    void setOutputContext(OutputRenderContext context) override;
    bool beginFrame(RenderTarget *target) override;
    bool endFrame() override;
    void cancelFrame() override;
    void prepareDamage(const base::Region &damage, const std::vector<base::Rect> &samples) override;
    bool repaints(const base::Rect &logical) const override;

    void waitFence(const SyncFence &fence) override;
    SyncFence createReleaseFence() override;

    void invalidateFrame();

    // For a CRTC without a CTM.
    void tintFrame(const ColorTransform &transform);

    // Otherwise the drawn frame takes a release fence.
    bool marksDrawn() const { return m_timer.available(); }

    // Without the submission a fence forces.
    GpuTimer::Mark markDrawn();

    // Empty until it has.
    std::optional<GpuTimer::Time> drawnAt(const GpuTimer::Mark &mark);

    void clear(const Color &color) override;
    void fillRect(const base::Rect &rect, const Color &color) override;
    void drawRoundedOutline(const RoundedShape &shape, const Color &color, double widthPx) override;
    void fillRectF(const base::RectF &rect, const Color &color) override;
    void drawTextureF(Texture *texture, const base::RectF &target, float alpha) override;
    void drawTextureTransformed(Texture *texture, const base::RectF &target, const base::AffineTransform &transform,
                                float alpha) override;
    bool keepsBackdrop(uint64_t scope, uint64_t owner) const override;
    std::optional<base::Rect> keptBackdropChange(uint64_t scope, uint64_t owner) const override;
    void beginBackdropPass(uint64_t scope, const std::vector<KeptBackdrop> &fresh) override;
    void drawBackgroundBlur(const base::Rect &bounds, int radius, const base::Rect &clip,
                            const GlassParams &glass = {}, const ui::Path &shape = {},
                            uint64_t owner = 0) override;
    void drawTexture(Texture *texture, const base::Rect &target, float alpha) override;
    void drawTextureMapped(Texture *texture, const base::Rect &target, float alpha,
                           const base::AffineTransform &mapping) override;
    void drawTextureMappedRects(Texture *texture, const std::vector<MappedRect> &rects,
                                float alpha) override;

    std::unique_ptr<Texture> createTexture(const base::Size &size, const uint8_t *pixels, int stride,
                                           PixelFormat format) override;
    bool updateTexture(Texture *texture, const uint8_t *pixels, int stride,
                       PixelFormat format) override;
    bool updateTexture(Texture *texture, const uint8_t *pixels, int stride, PixelFormat format,
                       const base::Region &damage) override;
    using Renderer::createTexture;

    ui::Bitmap captureFrame(const std::function<void()> &paint) override;
    ui::Bitmap captureImage(const OutputRenderContext &context, const base::Size &size,
                        const std::function<void()> &paint) override;
    std::shared_ptr<PendingImage> captureFrameLater(const std::function<void()> &paint) override;
    std::shared_ptr<PendingImage> captureImageLater(const OutputRenderContext &context,
                                                    const base::Size &size,
                                                    const std::function<void()> &paint) override;
    bool captureToDmaBuf(const OutputRenderContext &context, const base::Size &size,
                         const DmaBufAttributes &attributes,
                         const std::function<void()> &paint) override;
    const DmaBufFormats &captureDmaBufFormats() const override;
    dev_t captureDmaBufDevice() const override;
    bool copyOutputFrame(const base::Size &size) override;
    ui::Bitmap renderToImage(const base::Size &size, const std::function<void()> &paint) override;
    bool renderToTexture(std::unique_ptr<Texture> &texture, const base::Rect &bounds,
                         const std::function<void()> &paint) override;
    bool renderToTexture(std::unique_ptr<Texture> &texture, const base::Rect &bounds,
                         const base::Rect &repaint, const std::function<void()> &paint) override;
    void enableTrilinearFiltering(Texture *texture) override;
    void limitMipmapLevel(Texture *texture, int level) override;
    std::unique_ptr<Texture> renderShadow(const base::Size &size, const std::vector<ShadowLobe> &lobes,
                                          const std::function<void()> &paintCaster) override;

    const DmaBufFormats &dmaBufFormats() const override { return m_importer.formats(); }

    dev_t dmaBufMainDevice() const override { return m_importer.mainDevice(); }

    std::shared_ptr<Texture> importDmaBuf(const DmaBufAttributes &attributes) override;

    base::Size viewportSize() const override { return m_pass.viewport(); }

    EGLDisplay eglDisplay() const { return m_context->display(); }

    EGLConfig eglConfig() const { return m_context->config(); }

    const std::string &glesRenderer() const { return m_context->renderer(); }

    const std::string &glesVersion() const { return m_context->glVersion(); }

private:
    friend class EglRendererTestAccess;

    static constexpr std::chrono::seconds kBackdropReleaseDelay {5};

    explicit EglRenderer(std::shared_ptr<EglContext> context, gbm_device *device = nullptr);
    bool initPrograms(const char *fragmentOverride = nullptr,
                      const char *externalOverride = nullptr,
                      const char *backdropOverride = nullptr);
    bool makeCurrent(EglRenderTarget *target);
    bool makeContextCurrent();
    void applyRepaint(const base::Rect &repaint);
    void scheduleBackdropRelease();
    void releaseUnshownBackdrops();

    // Compared here and uploaded only when they differ, since most draws in a
    // row want the same clip stack.
    struct ClipUniforms {
        GLuint program = 0;
        GLint count = -1;
        GLint clips = -1;
        GLint clipRadii = -1;
        GLint viewportRect = -1;
        GLint outputTransform = -1;
        GLint outlineBounds = -1;
        GLint outlineRadius = -1;
        GLint outlineWidth = -1;
        GLint continuousCorners = -1;
        struct State {
            int count = 0;
            float bounds[16 * 4] = {};
            float radii[16 * 2] = {};
            GLint viewport[4] = {};
            uint32_t transform = 0;
            float outline[4] = {};
            float outlineRadii[2] = {};
            float outlineWidth = 0;
            float continuous = 0;
            bool operator==(const State &) const = default;
        };
        std::optional<State> uploaded;
    };
    ClipUniforms &clipUniforms(const GlProgram &program);

    // Resolved once and uploaded only when a draw changes them.
    struct TextureUniforms {
        GLint texture = -1;
        GLint alpha = -1;
        GLint swizzle = -1;
        GLint hasAlpha = -1;
        float lastAlpha = -1;
        float lastSwizzle = -1;
        float lastHasAlpha = -1;
    };
    void resolveTextureUniforms(TextureUniforms &uniforms, const GlProgram &program);
    void drawTextureMappedF(Texture *texture, const base::RectF &target, float alpha,
                            const base::AffineTransform &mapping, const base::AffineTransform &geometry = base::AffineTransform());

    // Positions in clip space and texture coordinates, in quadFor()'s corner
    // order.
    struct TextureQuad {
        float vertices[8];
        float texCoords[8];
    };
    void alignTextureDraw(const Texture *texture, const base::RectF &target, const base::AffineTransform &mapping,
                          base::RectF &aligned, base::AffineTransform &texelMapping) const;
    void textureQuad(const GlTexture &texture, const base::RectF &target, const base::AffineTransform &mapping,
                     const base::AffineTransform &geometry, TextureQuad &quad) const;
    void drawTextureQuads(GlTexture *texture, const TextureQuad *quads, size_t count,
                          float alpha);
    void applyRoundedClip(const GlProgram &program);
    void fillColorRects(std::initializer_list<base::RectF> rects, const Color &color);
    void quadFor(const base::RectF &rect, float *vertices) const;
    // Corners in triangle-strip order: top left, top right, bottom left, bottom right.
    void quadFor(const std::array<base::PointF, 4> &corners, float *vertices) const;

    std::shared_ptr<EglContext> m_context;
    EglFence m_fences;
    GpuTimer m_timer;
    EglDmaBufImporter m_importer;
    gbm_device *m_device = nullptr;
    mutable std::optional<DmaBufFormats> m_captureFormats;
    mutable dev_t m_captureDevice = 0;
    GlTextureUploader m_upload;
    GlRenderPass m_pass;
    std::unique_ptr<BackdropFilter> m_backdrop;
    std::unique_ptr<DropShadow> m_dropShadow;
    GlProgram m_colorProgram;
    GlProgram m_textureProgram;
    GlProgram m_externalProgram;
    std::vector<ClipUniforms> m_clipUniforms;
    GLint m_colorUniform = -1;
    TextureUniforms m_textureUniforms;
    TextureUniforms m_externalUniforms;
    RoundedShape m_outlineShape;
    double m_outlineWidth = 0;
    EglSwapState m_swapState;
    // The scissor of a partial repaint, in target pixels.
    std::optional<base::Rect> m_repaint;
    unsigned int m_frameFramebuffer = 0;
    EglRenderTarget *m_current = nullptr;
    EglRenderTarget *m_lastTarget = nullptr;
    // Made on the loop of the thread that draws.
    std::unique_ptr<base::Timer> m_backdropRelease;
    std::chrono::milliseconds m_backdropReleaseDelay = kBackdropReleaseDelay;
    uint64_t m_framesEnded = 0;
    uint64_t m_framesAtRelease = 0;
};

} // namespace cutefish::render

#endif // CUTEFISH_RENDER_EGL_RENDERER_H_
