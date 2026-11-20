// Copyright 2013 The Chromium Authors
// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/window_animations.cc
//           chromium/src/ui/wm/core/window_animations.cc
//           chromium/src/ui/gfx/animation/tween.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_SCENE_WINDOW_ANIMATION_H_
#define CUTEFISH_SCENE_WINDOW_ANIMATION_H_

#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "scene/damage_tracker.h"
#include "scene/node.h"
#include "scene/window_visual_node.h"
#include "base/geometry.h"
#include "ui/bitmap.h"

namespace cutefish::scene {

// The parent scene owns the node until its last frame.
class WindowAnimation : public Node
{
public:
    enum class Type {
        Open,
        Close,
        Minimize,
        Restore,
        Bounds,
        DragBounds
    };

    enum class Style {
        Window,
        Launchpad,
        Popup,
        Fade,
        None
    };

    static constexpr bool isCrossFade(Type type)
    {
        return type == Type::Bounds || type == Type::DragBounds;
    }

    struct Sample {
        base::RectF bounds;
        float opacity;
        double progress;
    };

    explicit WindowAnimation(std::function<double()> clock = {});
    void finish();
    void skip(Type type);
    void releaseRenderResources() override;
    void start(Type type, const base::Rect &from, const base::Rect &to, bool requestFrame = true);
    void close();
    void setBoundsContentReady(bool ready);

    void setStyle(Style style);

    Style style() const { return m_style; }

    // A no-op unless a DragBounds animation is running.
    void setDragTarget(const base::Rect &bounds);

    // A no-op unless a Bounds animation is running.
    void setBoundsTarget(const base::Rect &bounds);

    bool isWaitingForBoundsContent() const { return !m_boundsContentReady; }

    // A preview on the first minimize frame, then the landing image before the finished callback.
    void requestSnapshot(std::function<void(const ui::Bitmap &)> callback, int maximumPixels);

    void setMinimizeTarget(const base::Rect &target);

    // Holds the last frame on the dock slot until releaseHold().
    void setHoldAtEnd(bool hold) { m_holdAtEnd = hold; }
    void releaseHold();

    bool isHolding() const { return m_holding; }

    bool drawsAbovePanels() const { return isDockTransition(); }

    void setFinishedCallback(std::function<void()> callback) { m_finished = std::move(callback); }

    bool isRunning() const { return m_running; }

    Type type() const { return m_type; }

    const base::RectF &targetBounds() const { return m_to; }

    // Its surfaces still get frames at zero.
    float opacity() const { return m_opacity; }

    void setOpacity(float opacity);

    // `anchor` is in the window's own geometry.
    void setPresentationScale(double scale, const base::PointF &anchor, bool animate);

    double presentationScale() const;

    double targetPresentationScale() const { return m_scaleTo; }

    void render(render::Renderer &renderer) const override;

    // `radius` is in logical pixels.
    struct Presentation {
        base::RectF bounds;
        float opacity = 1;
        double uniform = 0;
        double radius = 0;
    };

    void renderPresented(render::Renderer &renderer, const Presentation &presentation) const;
    render::RoundedShape presentedBody(const Presentation &presentation) const;

    bool isRetired() const override { return m_closing && !m_running; }

    void finishRetiring() override
    {
        Node::finishRetiring();
        if (m_closing && m_running)
            finish();
    }

    bool paintsSubtree() const override
    {
        return animating() || m_opacity < 1 || presentedScaled();
    }

    bool requiresFullRepaint() const override
    {
        return animating() && (isCrossFade(m_type) || m_type == Type::Minimize);
    }

    bool paintsWhenHidden() const override
    {
        return !isRetired()
                && (m_closing || (m_type == Type::Minimize && (m_running || m_holding)));
    }

    bool redrawsSubtree(const base::Rect &viewport, double scale) const override;

    bool renderSiblingsBelow(render::Renderer &renderer, size_t begin,
                             size_t end) const override;

    Content content() const override { return Content::Translucent; }

    base::Rect absolutePaintBounds() const override;

    static Sample sample(Type type, const base::RectF &from, const base::RectF &to, double progress,
                         Style style = Style::Window);
    static int duration(Type type, const base::RectF &from, const base::RectF &to,
                        Style style = Style::Window);

private:
    bool animating() const { return m_running || m_holding || m_firstFrame || m_closing; }
    bool presentedScaled() const { return m_scaleFrom != 1 || m_scaleTo != 1; }
    void changed() const;
    Sample current() const;
    double now() const { return m_frameTime >= 0 ? m_frameTime : m_now(); }
    void readSnapshot(render::Renderer &renderer, bool landing = false) const;
    bool isDockTransition() const
    {
        return (m_running || m_holding) && m_style == Style::Window
                && (m_type == Type::Minimize || m_type == Type::Restore);
    }
    float exteriorOpacity(const Sample &sample) const;
    render::RoundedShape bodyShape(const base::Rect &original, const base::RectF &bounds) const;
    static render::RoundedShape transformedShape(render::RoundedShape shape, const base::Rect &original,
                                                 const Sample &sample);
    void outline(render::Renderer &renderer, const WindowVisualState &visual, const base::Rect &original,
                 const Sample &sample) const;
    bool morphsExterior() const;
    double morphedRadius(const Sample &sample) const;
    void drawExteriorMorph(render::Renderer &renderer, const Sample &value, float blend) const;
    mutable WindowVisualState m_visual;
    WindowVisualState m_oldVisual;
    void draw(render::Renderer &renderer, render::Texture *texture, const base::Rect &coverage,
              const base::Rect &original, const Sample &sample, float alpha = 1) const;
    base::Rect textureCoverage() const;
    bool textureReusable(const base::Rect &coverage, const base::Rect &viewport, double scale) const;
    // `repaint` receives the part to draw again, or empty for all of it.
    bool textureIsCurrent(render::Renderer &renderer, const base::Rect &coverage,
                          base::Rect *repaint) const;
    bool hasBackgroundBlurs() const;
    bool drawsDirectly(bool contentChanged) const;
    bool hasOpaqueSurface() const;
    bool settled() const;
    static constexpr int kDirectRefresh = 8;

    mutable std::optional<Presentation> m_presentedAt;
    render::RoundedShape presentedBody(const Presentation &presentation,
                                       const base::RectF &bounds) const;
    void drawPresented(render::Renderer &renderer, const Sample &value) const;
    void drawPresentedBlurs(render::Renderer &renderer, const Sample &value) const;
    bool isPresentedHidden() const { return m_presentedAt && !isVisible(); }

    mutable std::unique_ptr<render::Texture> m_texture;
    mutable bool m_textureBlurSuppressed = false;
    mutable bool m_textureMipmapped = false;
    mutable DamageTracker m_textureDamage {true};
    mutable bool m_textureReusable = false;
    mutable int m_directFrames = 0;
    mutable bool m_contentChanged = false;
    mutable double m_directFrameTime = -1;
    mutable double m_textureDrawnAt = 0;
    mutable double m_textureScale = 0;
    mutable base::Rect m_textureViewport;
    mutable uint64_t m_textureRootRevision = 0;
    struct StashedTexture {
        std::unique_ptr<render::Texture> texture;
        bool blurSuppressed = false;
        bool mipmapped = false;
        DamageTracker damage {true};
        bool reusable = false;
        double scale = 0;
        base::Rect viewport;
        uint64_t rootRevision = 0;
        base::Rect coverage;
        base::Rect bounds;
    };
    mutable std::unique_ptr<StashedTexture> m_stashedTexture;
    void swapStashedTexture(bool blurSuppressed) const;
    struct BelowTexture {
        std::unique_ptr<render::Texture> texture;
        DamageTracker damage {true};
        double scale = 0;
        base::Rect viewport;
        uint64_t rootRevision = 0;
    };
    mutable std::map<std::string, BelowTexture> m_belowTextures;
    mutable std::unique_ptr<render::Texture> m_landingTexture;
    mutable std::unique_ptr<render::Texture> m_oldTexture;
    mutable base::Rect m_coverage;
    mutable base::Rect m_textureBounds;
    base::Rect m_oldCoverage;
    base::Rect m_oldBounds;
    base::RectF m_from;
    base::RectF m_to;
    base::PointF m_dragOffset;
    base::SizeF m_dragScale {1, 1};
    Type m_type = Type::Open;
    Style m_style = Style::Window;
    std::optional<Sample> m_handoff;
    std::function<double()> m_now;
    mutable double m_frameTime = -1;
    mutable double m_framePeriod = 0;
    mutable double m_started = -1;
    mutable bool m_firstStep = false;
    mutable double m_firstStepAt = -1;
    mutable bool m_running = false;
    mutable bool m_firstFrame = true;
    bool m_closing = false;
    bool m_boundsContentReady = true;
    float m_startOpacity = 1;
    float m_opacity = 1;
    mutable double m_scaleFrom = 1;
    double m_scaleTo = 1;
    mutable double m_scaleStarted = -1;
    base::PointF m_scaleAnchor;
    mutable Sample m_presented {{}, 1, 0};
    mutable std::function<void(const ui::Bitmap &)> m_snapshot;
    mutable std::function<void(const ui::Bitmap &)> m_landingSnapshot;
    int m_snapshotPixels = 0;
    std::function<void()> m_finished;
    bool m_holdAtEnd = false;
    mutable bool m_holding = false;
    base::RectF m_dockWindow;
    base::RectF m_dockSlot;

    struct TargetAdjustment {
        base::PointF position;
        base::SizeF size;
        double progress;
    };

    std::vector<TargetAdjustment> m_minimizeAdjustments;
};

} // namespace cutefish::scene

#endif // CUTEFISH_SCENE_WINDOW_ANIMATION_H_
