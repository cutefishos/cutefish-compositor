// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SCENE_OVERVIEW_NODE_H_
#define CUTEFISH_SCENE_OVERVIEW_NODE_H_

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <string>

#include "scene/finger_track.h"
#include "scene/motion.h"
#include "scene/node.h"
#include "base/geometry.h"
#include "ui/bitmap.h"

namespace cutefish::scene {

class WindowAnimation;

// `density` is device pixels per logical one; returns premultiplied RGBA8888, or null.
using OverviewTextPainter = std::function<ui::Bitmap(const std::string &text, int pixelSize,
                                                     const render::Color &color, double density,
                                                     int maxWidth)>;

// `radius` is in logical pixels.
struct OverviewWindowFrame {
    double radius = 0;
    std::function<void(render::Renderer &renderer, const WindowAnimation &window,
                       const render::RoundedShape &body, double uniform, float opacity,
                       bool moving)>
            shadow;
    std::function<void(render::Renderer &renderer, const render::RoundedShape &body,
                       float opacity)>
            outline;
};

class OverviewChromeCache
{
public:
    render::Texture *label(render::Renderer &renderer, const OverviewTextPainter &painter,
                           const std::string &text, int pixelSize, const render::Color &color,
                           int maxWidth, base::SizeF *size);
    enum class Glyph {
        Close,
        Plus,
    };
    // `size` is in logical pixels.
    render::Texture *glyph(render::Renderer &renderer, Glyph glyph, const base::SizeF &size,
                           const render::Color &color);
    void clear() { m_entries.clear(); }

private:
    struct Entry {
        std::string key;
        std::unique_ptr<render::Texture> texture;
        base::SizeF size;
    };
    render::Texture *find(const std::string &key, base::SizeF *size);
    render::Texture *insert(render::Renderer &renderer, const std::string &key, const ui::Bitmap &image,
                            const base::SizeF &size);
    std::list<Entry> m_entries;
};

// Progress 0 is the desk as it is, 1 the overview.
class OverviewNode : public Node
{
public:
    struct Item {
        // Bottom to top; the first one's geometry is what `to` places.
        std::vector<const WindowAnimation *> windows;
        base::RectF to;
        std::string title;
    };

    struct Chrome {
        int hovered = -1;
        int selected = -1;
    };

    static constexpr double kTransitionDurationMs = 250;
    static constexpr double kLayoutDurationMs = 300;
    // In progress per second.
    static constexpr double kFlickVelocity = 0.5;
    static constexpr double kChromeFadeMs = 150;
    static constexpr float kDimOpacity = 0.35f;

    explicit OverviewNode(std::function<int64_t()> clock = {});

    void setTextPainter(OverviewTextPainter painter) { m_textPainter = std::move(painter); }
    void setWindowFrame(OverviewWindowFrame frame) { m_frame = std::move(frame); }

    void setBackdrop(std::vector<const Node *> nodes);
    void setFadingBackdrop(std::vector<const Node *> nodes);

    void setItems(std::vector<Item> items, bool animate);
    const std::vector<Item> &items() const { return m_items; }

    // None for -1. Drawn by renderDragged(), over the desk bar.
    void setDragged(int index, const base::RectF &rect);
    void renderDragged(render::Renderer &renderer) const;

    // None for -1.
    void setRaised(int index);

    void setChrome(const Chrome &chrome);

    // `timeMs` is on the frame clock.
    double progress(double timeMs) const;
    double progress() const { return progress(clockTime()); }
    double clockTime() const { return double(m_now()); }

    void animateTo(double target);
    void jumpTo(double value);
    double target() const;

    void startTracking();
    void updateTracking(double delta, double timeMs);
    // Returns where the progress heads, 0 or 1.
    double endTracking(double timeMs);
    bool isTracking() const { return m_motion == Motion::Tracking; }
    bool isSettled() const { return m_motion == Motion::Still; }

    bool isActive() const { return m_motion != Motion::Still || m_position > 0; }

    void setContentHidden(bool hidden);
    bool covers() const { return isActive() && !m_contentHidden; }

    // Runs from inside render(): must not change the scene.
    void setFinishedCallback(std::function<void(double)> callback)
    {
        m_finished = std::move(callback);
    }

    base::RectF presentedRect(int index, double progress, double timeMs) const;

    void render(render::Renderer &renderer) const override;
    Content content() const override { return covers() ? Content::Opaque : Content::None; }
    bool paintsSubtree() const override { return covers(); }
    bool requiresFullRepaint() const override { return covers(); }
    bool acceptsInput(const base::PointF &) const override { return false; }
    bool occludesOutput(const render::Renderer &) const override { return covers(); }
    void releaseRenderResources() override;

private:
    enum class Motion {
        Still,
        Tracking,
        Tween,
    };

    motion::Tween tweenTo(double now, double from, double target, double velocity) const;
    void settle(double now, double from, double target, double velocity);
    void updateCoast();
    double layoutProgress(double timeMs) const;
    bool backdropCovers(const base::Rect &viewport) const;
    float chromeOpacity(double timeMs) const;
    void renderItem(render::Renderer &renderer, int index, double progress, double now) const;
    render::RoundedShape restingBody(int index, const base::RectF &rect) const;
    void renderChrome(render::Renderer &renderer, const base::Rect &viewport, double now) const;
    void landed(double now) const;
    void advance(double now) const;

    std::function<int64_t()> m_now;
    OverviewTextPainter m_textPainter;
    OverviewWindowFrame m_frame;
    std::vector<const Node *> m_backdrop;
    std::vector<const Node *> m_fadingBackdrop;
    std::vector<Item> m_items;
    std::vector<base::RectF> m_layoutFrom;
    std::optional<motion::Tween> m_layout;
    int m_dragged = -1;
    base::RectF m_dragRect;
    int m_raised = -1;
    bool m_contentHidden = false;
    Chrome m_chrome;

    mutable Motion m_motion = Motion::Still;
    mutable double m_position = 0;
    motion::FingerTrack m_fingers;
    std::optional<motion::Tween> m_coast;
    double m_coastStart = 0;
    motion::Tween m_tween;
    mutable double m_shownAt = -1;
    std::function<void(double)> m_finished;
    mutable OverviewChromeCache m_chromeCache;
};

class OverviewDeskBarNode : public Node
{
public:
    struct Desk {
        base::RectF bounds;
        std::string name;
        bool active = false;
        bool highlighted = false;
        bool pressed = false;
        bool dragged = false;
        bool closeShown = false;
        bool closeHovered = false;
        uintptr_t id = 0;
    };

    struct Bar {
        base::Rect output;
        base::RectF bounds;
        std::vector<Desk> desks;
        base::RectF newDesk;
        bool newDeskHighlighted = false;
    };

    struct DeskContent {
        uintptr_t id = 0;
        std::vector<const WindowAnimation *> windows;
    };

    static constexpr double kCloseButtonSize = 24;
    static constexpr double kCloseButtonMargin = 4;
    static base::RectF closeButtonRect(const base::RectF &preview);

    static constexpr double kPreviewRadius = 8;
    static constexpr int kLabelPixelSize = 13;
    static constexpr double kPreviewRefreshMs = 500;
    static constexpr int kPreviewsPerFrame = 1;

    static constexpr double kShiftDurationMs = 150;
    static constexpr double kAddScaleDelayMs = 50;
    static constexpr double kAddScaleDurationMs = 150;
    static constexpr double kAddInitialScale = 0.1;
    static constexpr double kAddFadeDelayMs = 100;
    static constexpr double kAddFadeDurationMs = 100;
    static constexpr double kRemoveFadeDurationMs = 200;

    explicit OverviewDeskBarNode(const OverviewNode *overview);

    void setTextPainter(OverviewTextPainter painter) { m_textPainter = std::move(painter); }
    void setBars(std::vector<Bar> bars);
    void setContent(std::vector<const Node *> backdrop, std::vector<DeskContent> desks);
    void invalidatePreviews();

    static double slideOffset(const Bar &bar, double progress);

    void render(render::Renderer &renderer) const override;
    Content content() const override
    {
        return m_overview->isActive() ? Content::Translucent : Content::None;
    }
    bool paintsSubtree() const override { return m_overview->isActive(); }
    bool requiresFullRepaint() const override { return m_overview->isActive(); }
    bool acceptsInput(const base::PointF &) const override { return false; }
    void releaseRenderResources() override;

private:
    struct Preview {
        std::unique_ptr<render::Texture> texture;
        base::RectF bounds;
        double scale = 0;
        double drawnAt = -1;
        bool stale = true;
    };

    struct Placement {
        base::RectF from;
        std::optional<motion::Tween> shift;
        std::optional<motion::Tween> scale;
        std::optional<motion::Tween> fade;

        static Placement appearing(double now);
        base::RectF rect(const base::RectF &to, double now) const;
        double scaleAt(double now) const { return scale ? scale->value(now) : 1; }
        float opacity(double now) const { return fade ? float(fade->value(now)) : 1.0f; }
        bool isMoving(double now) const;
        void moveTo(const base::RectF &oldTo, const base::RectF &to, double now);
    };

    struct BarMotion {
        std::map<uintptr_t, Placement> desks;
        Placement newDesk;
    };

    struct Leaving {
        Bar bar;
        std::optional<Desk> desk;
        base::RectF rect;
        double scale = 1;
        std::unique_ptr<render::Texture> preview;
        motion::Tween fade;
    };

    void renderBar(render::Renderer &renderer, const Bar &bar, const BarMotion *motion,
                   double progress, double now) const;
    void renderDesk(render::Renderer &renderer, const Desk &desk, const base::RectF &rect,
                    double scale, render::Texture *preview, float opacity) const;
    void renderNewDesk(render::Renderer &renderer, const base::RectF &rect, bool highlighted,
                       float opacity) const;
    void updatePreview(render::Renderer &renderer, const Bar &bar, int desk, double now) const;

    const OverviewNode *m_overview = nullptr;
    OverviewTextPainter m_textPainter;
    std::vector<Bar> m_bars;
    std::vector<BarMotion> m_motions;
    mutable std::vector<Leaving> m_leaving;
    std::vector<const Node *> m_backdrop;
    std::vector<DeskContent> m_desks;
    mutable std::map<std::pair<std::string, uintptr_t>, Preview> m_previews;
    mutable int m_previewsLeft = 0;
    mutable OverviewChromeCache m_chromeCache;
};

} // namespace cutefish::scene

#endif // CUTEFISH_SCENE_OVERVIEW_NODE_H_
