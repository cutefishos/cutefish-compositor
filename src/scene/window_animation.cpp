// Copyright 2013 The Chromium Authors
// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/window_animations.cc
//           chromium/src/ui/wm/core/window_animations.cc
//           chromium/src/ui/gfx/animation/tween.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include "scene/window_animation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <utility>


#include "scene/motion.h"
#include "scene/surface_node.h"
#include "base/region.h"
#include "base/transform.h"

namespace cutefish::scene {

namespace {

base::RectF shrink(const base::RectF &bounds, double scale)
{
    const base::SizeF size = bounds.size() * scale;
    return base::RectF(bounds.center() - base::PointF(size.width(), size.height()) / 2, size);
}

double smoothStep(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * t * (10 + t * (-15 + 6 * t));
}

double easeOut(double t, double power)
{
    return 1 - std::pow(1 - std::clamp(t, 0.0, 1.0), power);
}

constexpr double kLaunchpadZoom = 1.05;

double accel20Decel60(double t)
{
    return motion::cubicBezier(.2, 0, .4, 1, t);
}

// Spring responses in seconds.
constexpr double kOpenMotion = .36;
constexpr double kOpenFade = .12;
constexpr double kCloseMotion = .26;
constexpr double kCloseFade = .16;
constexpr int kOpenDurationMs = 360;
constexpr int kCloseDurationMs = 220;
constexpr int kScaleDurationMs = 250;
constexpr double kCrossFadeContentShare = 1.0 / 3;

// `elapsed` and `duration` are in milliseconds.
double springStep(double response, double elapsed, double duration)
{
    const double t = elapsed / duration;
    if (t <= 0)
        return 0;
    if (t >= 1)
        return 1;
    motion::Spring spring({response, 1});
    spring.reset(0, 0, 1);
    return std::clamp(spring.value(elapsed / (1 - t * t)), 0.0, 1.0);
}

base::Rect subtreeBounds(const Node *node)
{
    base::Rect bounds = node->absolutePaintBounds();
    for (const Node *child : node->children()) {
        if (child->isVisible())
            bounds = bounds.united(subtreeBounds(child));
    }
    return bounds;
}

bool hasPromotedSurface(const Node &node)
{
    if (node.isPromotedToOverlay())
        return true;
    return std::any_of(node.children().begin(), node.children().end(), [](const Node *child) {
        return hasPromotedSurface(*child);
    });
}

void recordPresentation(render::Renderer &renderer, const Node &node)
{
    if (!node.isVisible())
        return;
    if (const auto *surface = dynamic_cast<const SurfaceNode *>(&node);
        surface && surface->texture() && surface->alpha() > 0
        && !node.absoluteGeometry().isEmpty())
        renderer.recordPresentation(surface->presentationContent(), node.absoluteGeometry());
    for (const Node *child : node.children())
        recordPresentation(renderer, *child);
}

const Node &sceneRoot(const Node &node)
{
    const Node *root = &node;
    while (root->parent())
        root = root->parent();
    return *root;
}

base::Rect outputViewport(const render::Renderer &renderer, const Node &node)
{
    const base::Rect viewport = renderer.outputContext().logicalViewport;
    return viewport.isEmpty() ? sceneRoot(node).geometry() : viewport;
}

} // namespace

WindowAnimation::WindowAnimation(std::function<double()> clock)
    : Node("window")
    , m_now(clock ? std::move(clock) : [] {
        return std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
    })
{
}

int WindowAnimation::duration(Type type, const base::RectF &from, const base::RectF &to, Style style)
{
    if (style == Style::Launchpad && type == Type::Open)
        return 360;
    if (style == Style::Launchpad && type == Type::Close)
        return 160;
    if (style == Style::Popup && type == Type::Open)
        return 100;
    if (style == Style::Popup && type == Type::Close)
        return 80;
    if (type == Type::Open)
        return base::roundToInt(kOpenDurationMs * motion::durationScale());
    if (type == Type::Close)
        return base::roundToInt(kCloseDurationMs * motion::durationScale());
    if (type == Type::Minimize)
        return 320;
    if (type == Type::Restore)
        return 320;
    if (type == Type::DragBounds)
        return 120;
    const double a = from.width() * from.height();
    const double b = to.width() * to.height();
    const double largest = std::max(a, b);
    return 200 + (largest > 0 ? base::roundToInt(200 * std::abs(a - b) / largest) : 0);
}

WindowAnimation::Sample WindowAnimation::sample(Type type, const base::RectF &from, const base::RectF &to,
                                                double progress, Style style)
{
    const double t = std::clamp(progress, 0.0, 1.0);
    if (style == Style::Launchpad && (type == Type::Open || type == Type::Close)) {
        const double eased = type == Type::Open ? easeOut(t, 4) : t + .6 * t * (1 - t);
        const float opacity = type == Type::Open ? easeOut(t / .6, 3) : 1 - t;
        return {base::RectF(from.topLeft() + (to.topLeft() - from.topLeft()) * eased,
                       from.size() + (to.size() - from.size()) * eased),
                opacity, t};
    }
    if (style == Style::Popup && (type == Type::Open || type == Type::Close)) {
        const float opacity = type == Type::Open ? easeOut(t, 2) : 1 - easeOut(t, 2);
        return {to, opacity, t};
    }
    if (style == Style::Fade && (type == Type::Open || type == Type::Close)) {
        const float opacity = type == Type::Open ? smoothStep(t / .55) : 1 - smoothStep(t);
        return {to, opacity, t};
    }
    double eased = type == Type::DragBounds ? t : accel20Decel60(t);
    float opacity = 1;
    if (type == Type::Restore) {
        opacity = smoothStep(t / .3);
    } else if (type == Type::Open || type == Type::Close) {
        const double length = duration(type, from, to, style);
        const double elapsed = t * length;
        const bool open = type == Type::Open;
        eased = springStep(open ? kOpenMotion : kCloseMotion, elapsed, length);
        const double fade = springStep(open ? kOpenFade : kCloseFade, elapsed, length);
        opacity = open ? fade : 1 - fade;
    }
    return {base::RectF(from.topLeft() + (to.topLeft() - from.topLeft()) * eased,
                   from.size() + (to.size() - from.size()) * eased),
            opacity, t};
}

WindowAnimation::Sample WindowAnimation::current() const
{
    double length = duration(m_type, m_from, m_to, m_style);
    if (m_style == Style::Launchpad && m_type == Type::Close && m_framePeriod > 0)
        length = std::max(1.0, std::round(length / m_framePeriod)) * m_framePeriod;
    const double t = (m_started >= 0) ? (now() - m_started) / length : 0;
    Sample result = sample(m_type, m_from, m_to, t, m_style);
    if (m_type == Type::Minimize && result.progress < 1) {
        for (const auto &adjustment : m_minimizeAdjustments) {
            const double remaining = (result.progress - adjustment.progress)
                    / (1 - adjustment.progress);
            const double weight = smoothStep(remaining) - accel20Decel60(result.progress);
            result.bounds.translate(adjustment.position * weight);
            result.bounds.setSize(result.bounds.size() + adjustment.size * weight);
        }
    }
    if (m_type == Type::Close)
        result.opacity *= m_startOpacity;
    if (m_type == Type::Minimize)
        result.opacity = m_startOpacity + (1 - m_startOpacity) * smoothStep(result.progress / .6);
    if (m_type == Type::Restore || m_type == Type::Open)
        result.opacity = m_startOpacity + (1 - m_startOpacity) * result.opacity;
    if (isCrossFade(m_type))
        result.opacity = m_startOpacity + (1 - m_startOpacity) * smoothStep(result.progress);
    return result;
}

void WindowAnimation::finish()
{
    m_running = false;
    m_holding = false;
    m_firstFrame = false;
    m_boundsContentReady = true;
    m_oldTexture.reset();
    m_landingTexture.reset();
    m_snapshot = nullptr;
    m_landingSnapshot = nullptr;
    changed();
}

void WindowAnimation::skip(Type type)
{
    m_type = type;
    finish();
}

void WindowAnimation::changed() const
{
    notifyChanged();
    if (!isVisible() && parent())
        parent()->requestFrame();
}

void WindowAnimation::releaseRenderResources()
{
    finish();
    m_texture.reset();
    m_stashedTexture.reset();
    m_belowTextures.clear();
    m_textureReusable = false;
    Node::releaseRenderResources();
}

void WindowAnimation::start(Type type, const base::Rect &from, const base::Rect &to, bool requestFrame)
{
    if (m_style == Style::None) {
        skip(type);
        return;
    }
    const bool interrupted = m_running;
    const Sample previous = interrupted ? m_presented : Sample {from, 1, 0};
    m_from = interrupted ? previous.bounds : base::RectF(from);
    m_to = to;
    m_startOpacity =
            !interrupted && type == Type::Open ? 0 : previous.opacity;
    m_type = type;
    m_holding = false;
    m_minimizeAdjustments.clear();
    m_landingTexture.reset();
    m_snapshot = nullptr;
    m_landingSnapshot = nullptr;
    if (type == Type::Minimize) {
        m_dockWindow = from;
        m_dockSlot = to;
    } else if (type == Type::Restore) {
        m_dockWindow = to;
        m_dockSlot = from;
    }
    m_boundsContentReady = true;
    m_running = true;
    m_firstFrame = false;
    // Starts on the next presented frame, so a late event loop cannot skip the first frame.
    m_started = -1;
    const bool launchpad = m_style == Style::Launchpad;
    const bool inPlace = m_style == Style::Popup || m_style == Style::Fade;
    if (type == Type::Open && !interrupted)
        m_from = inPlace ? base::RectF(to) : shrink(to, launchpad ? kLaunchpadZoom : .94);
    if (type == Type::Close)
        m_to = inPlace ? m_from : launchpad ? shrink(from, kLaunchpadZoom) : shrink(m_from, .95);
    m_presented = {m_from, m_startOpacity, 0};
    if (type == Type::DragBounds && !base::RectF(to).isEmpty()) {
        m_dragOffset = base::RectF(from).topLeft() - base::RectF(to).topLeft();
        m_dragScale =
                base::SizeF(double(from.width()) / to.width(), double(from.height()) / to.height());
    }
    m_textureReusable = false;
    m_stashedTexture.reset();
    if (isCrossFade(type) && m_texture) {
        m_oldTexture = std::move(m_texture);
        m_oldCoverage = m_coverage;
        m_oldBounds = m_textureBounds;
        m_oldVisual = m_visual;
    }
    if (!isCrossFade(type)) {
        if (!m_texture && m_oldTexture) {
            m_texture = std::move(m_oldTexture);
            m_coverage = m_oldCoverage;
            m_textureBounds = m_oldBounds;
            m_visual = m_oldVisual;
        }
        m_oldTexture.reset();
    }
    if (requestFrame)
        changed();
}

void WindowAnimation::requestSnapshot(std::function<void(const ui::Bitmap &)> callback,
                                      int maximumPixels)
{
    m_snapshot = std::move(callback);
    m_landingSnapshot = m_snapshot;
    m_snapshotPixels = std::max(1, maximumPixels);
    changed();
}

void WindowAnimation::readSnapshot(render::Renderer &renderer, bool landing) const
{
    const auto callback = std::exchange(landing ? m_landingSnapshot : m_snapshot, nullptr);
    if (!callback || !m_texture || m_textureBounds.isEmpty())
        return;
    if (landing && (m_to.width() <= 1 || m_to.height() <= 1))
        return;
    base::SizeF pixels = (landing ? m_to.size() : base::SizeF(m_textureBounds.size())) * renderer.rasterScale();
    if (pixels.width() > m_snapshotPixels || pixels.height() > m_snapshotPixels) {
        const double width = m_snapshotPixels * pixels.width() / pixels.height();
        pixels = width <= m_snapshotPixels
                ? base::SizeF(width, m_snapshotPixels)
                : base::SizeF(m_snapshotPixels, m_snapshotPixels * pixels.height() / pixels.width());
    }
    const base::Size size(std::max(1, landing ? int(std::ceil(pixels.width())) : base::roundToInt(pixels.width())),
                     std::max(1, landing ? int(std::ceil(pixels.height())) : base::roundToInt(pixels.height())));
    const double sx = size.width() / double(m_textureBounds.width());
    const double sy = size.height() / double(m_textureBounds.height());
    const base::RectF target((m_coverage.x() - m_textureBounds.x()) * sx,
                        (m_coverage.y() - m_textureBounds.y()) * sy, m_coverage.width() * sx,
                        m_coverage.height() * sy);
    const double minification = std::min(m_texture->size().width() / target.width(),
                                          m_texture->size().height() / target.height());
    const int level = int(std::floor(std::log2(std::max(1.0, minification))));
    renderer.limitMipmapLevel(m_texture.get(), level);
    const ui::Bitmap image = renderer.renderToImage(size, [&] {
        renderer.pushRoundedClip(bodyShape(m_textureBounds, base::RectF(base::PointF(), size)));
        renderer.drawTextureF(m_texture.get(), target);
        renderer.popRoundedClip();
    });
    renderer.limitMipmapLevel(m_texture.get(), -1);
    if (!image.isNull()) {
        if (landing) {
            const ui::Bitmap rgba = image.converted(ui::Bitmap::Format::Rgba8888Premultiplied);
            m_landingTexture =
                    renderer.createTexture(rgba.size(), rgba.constBits(), int(rgba.stride()),
                                           render::PixelFormat::Rgba8888);
        }
        callback(image);
    }
}

void WindowAnimation::setMinimizeTarget(const base::Rect &target)
{
    if (m_type != Type::Minimize || (!m_running && !m_holding) || target.isEmpty()
        || base::fuzzyCompare(m_to, base::RectF(target)))
        return;
    if (m_running && m_started >= 0) {
        const double progress = current().progress;
        if (progress < 1) {
            m_minimizeAdjustments.push_back({base::RectF(target).topLeft() - m_to.topLeft(),
                                            base::SizeF(target.size()) - m_to.size(), progress});
        }
    }
    m_to = target;
    m_dockSlot = target;
    changed();
}

void WindowAnimation::releaseHold()
{
    if (!m_holding)
        return;
    m_holding = false;
    m_texture.reset();
    m_stashedTexture.reset();
    m_oldTexture.reset();
    m_landingTexture.reset();
    m_textureReusable = false;
    changed();
}

float WindowAnimation::exteriorOpacity(const Sample &sample) const
{
    if (!isDockTransition() || isPresentedHidden())
        return 1;
    const double range = m_dockWindow.width() - m_dockSlot.width();
    if (range <= 0)
        return 1;
    return std::clamp((sample.bounds.width() - m_dockSlot.width()) / range, double(0), double(1));
}

render::RoundedShape WindowAnimation::bodyShape(const base::Rect &original, const base::RectF &bounds) const
{
    if (m_visual.shape.bounds.isEmpty())
        return render::RoundedShape {bounds};
    return transformedShape(m_visual.shape, original, Sample {bounds, 1, 1});
}

bool WindowAnimation::morphsExterior() const
{
    const auto plain = [](const WindowVisualState &visual) {
        return visual.mode != WindowVisualMode::PreserveClient && visual.outlinePath.isEmpty()
                && visual.outlineShape.bounds.isEmpty() && !visual.shape.bounds.isEmpty();
    };
    return m_running && m_type == Type::Bounds && m_oldTexture && plain(m_visual)
            && plain(m_oldVisual);
}

double WindowAnimation::morphedRadius(const Sample &sample) const
{
    const double from = m_oldVisual.shape.radius;
    return from + (m_visual.shape.radius - from) * accel20Decel60(sample.progress);
}

void WindowAnimation::drawExteriorMorph(render::Renderer &renderer, const Sample &value,
                                        float blend) const
{
    const float progress = accel20Decel60(value.progress);
    const bool oldExterior = m_oldVisual.drawOutline;
    const bool newExterior = m_visual.drawOutline;
    render::RoundedShape body = transformedShape(m_visual.shape, m_textureBounds, value);
    body.radius = morphedRadius(value);
    body.radiusScale = base::SizeF(1, 1);

    if (oldExterior && !newExterior)
        draw(renderer, m_oldTexture.get(), m_oldCoverage, m_oldBounds, value, 1 - progress);
    const float exterior = newExterior ? (oldExterior ? 1 : progress) : 0;
    if (exterior > 0)
        draw(renderer, m_texture.get(), m_coverage, m_textureBounds, value, exterior);
    renderer.pushRoundedClip(body);
    if (exterior < 1)
        draw(renderer, m_texture.get(), m_coverage, m_textureBounds, value);
    if (blend < 1)
        draw(renderer, m_oldTexture.get(), m_oldCoverage, m_oldBounds, value, 1 - blend);
    renderer.popRoundedClip();

    const float outline = (oldExterior ? 1 - progress : 0) + (newExterior ? progress : 0);
    if (outline > 0 && !renderer.suppressWindowOutlines)
        paintWindowOutline(renderer, body, newExterior ? m_visual : m_oldVisual,
                           outline * value.opacity);
}

void WindowAnimation::setDragTarget(const base::Rect &bounds)
{
    if (m_type != Type::DragBounds || !m_running || bounds.isEmpty()
        || base::fuzzyCompare(m_to, base::RectF(bounds)))
        return;
    m_to = bounds;
    m_from = base::RectF(
            m_to.topLeft() + m_dragOffset,
            base::SizeF(m_to.width() * m_dragScale.width(), m_to.height() * m_dragScale.height()));
    changed();
}

void WindowAnimation::setBoundsTarget(const base::Rect &bounds)
{
    if (m_type != Type::Bounds || !m_running || bounds.isEmpty()
        || base::fuzzyCompare(m_to, base::RectF(bounds)))
        return;
    m_to = bounds;
    changed();
}

void WindowAnimation::setBoundsContentReady(bool ready)
{
    if (!isCrossFade(m_type) || m_boundsContentReady == ready)
        return;
    m_boundsContentReady = ready;
    changed();
}

void WindowAnimation::setStyle(Style style)
{
    m_style = style;
    if (style != Style::Launchpad || !m_firstFrame || !parent())
        return;
    for (Node *sibling : parent()->children()) {
        auto *closing = dynamic_cast<WindowAnimation *>(sibling);
        if (!closing || closing == this || closing->m_style != Style::Launchpad
            || !closing->m_closing || !closing->m_running || closing->m_textureBounds.isEmpty())
            continue;
        const base::RectF snapshot = closing->m_textureBounds;
        const base::RectF presented = closing->m_presented.bounds;
        m_handoff = Sample {base::RectF((presented.x() - snapshot.x()) / snapshot.width(),
                                   (presented.y() - snapshot.y()) / snapshot.height(),
                                   presented.width() / snapshot.width(),
                                   presented.height() / snapshot.height()),
                            closing->m_presented.opacity, 0};
        // Retired before either paints again, so no frame draws both.
        closing->m_running = false;
        closing->changed();
        return;
    }
}

void WindowAnimation::setOpacity(float opacity)
{
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (m_opacity == opacity)
        return;
    m_opacity = opacity;
    changed();
}

void WindowAnimation::setPresentationScale(double scale, const base::PointF &anchor, bool animate)
{
    m_scaleFrom = animate ? presentationScale() : scale;
    m_scaleTo = scale;
    m_scaleStarted = -1;
    m_scaleAnchor = anchor;
    m_textureReusable = false;
    changed();
}

double WindowAnimation::presentationScale() const
{
    if (m_scaleFrom == m_scaleTo || m_scaleStarted < 0)
        return m_scaleFrom;
    const double t = (now() - m_scaleStarted) / (kScaleDurationMs * motion::durationScale());
    return t >= 1 ? m_scaleTo : m_scaleFrom + (m_scaleTo - m_scaleFrom) * easeOut(t, 3);
}

void WindowAnimation::close()
{
    if (!isVisible() || (m_type == Type::Minimize && !m_running) || (!m_texture && !m_oldTexture)) {
        m_running = false;
        m_holding = false;
        m_closing = true;
        changed();
        return;
    }
    base::Rect &bounds = m_texture ? m_textureBounds : m_oldBounds;
    base::Rect &coverage = m_texture ? m_coverage : m_oldCoverage;
    const base::RectF drawn = m_presented.bounds;
    if (!m_running && base::fuzzyCompare(drawn.size(), base::SizeF(bounds.size()))) {
        const base::Point moved = drawn.topLeft().toPoint() - bounds.topLeft();
        bounds.translate(moved);
        coverage.translate(moved);
    }
    const base::Rect snapshot = bounds;
    start(Type::Close, snapshot, snapshot);
    m_closing = true;
}

void WindowAnimation::draw(render::Renderer &renderer, render::Texture *texture,
                           const base::Rect &coverage, const base::Rect &original, const Sample &value,
                           float alpha) const
{
    if (!texture || original.isEmpty())
        return;
    const double sx = value.bounds.width() / original.width();
    const double sy = value.bounds.height() / original.height();
    const base::Point offset = coverage.topLeft() - original.topLeft();
    const base::RectF target(value.bounds.topLeft() + base::PointF(offset.x() * sx, offset.y() * sy),
                        base::SizeF(coverage.width() * sx, coverage.height() * sy));
    if (!renderer.drawsInto(target.toEnclosingRect()))
        return;
    renderer.drawTextureF(texture, target, value.opacity * alpha * renderer.opacityScale);
}

render::RoundedShape WindowAnimation::transformedShape(render::RoundedShape shape,
                                                       const base::Rect &original, const Sample &sample)
{
    if (original.isEmpty())
        return shape;
    const double sx = sample.bounds.width() / original.width();
    const double sy = sample.bounds.height() / original.height();
    shape.bounds =
            base::RectF(sample.bounds.topLeft() + base::PointF(shape.bounds.x() * sx, shape.bounds.y() * sy),
                   base::SizeF(shape.bounds.width() * sx, shape.bounds.height() * sy));
    shape.radiusScale = base::SizeF(sx, sy);
    return shape;
}

void WindowAnimation::outline(render::Renderer &renderer, const WindowVisualState &visual,
                              const base::Rect &original, const Sample &sample) const
{
    if (!visual.drawOutline || renderer.suppressWindowOutlines)
        return;
    const base::PointF snap = clientBodySnap(renderer, visual, original.topLeft());
    if (!visual.outlinePath.isEmpty() && !original.isEmpty()) {
        if (base::fuzzyCompare(sample.bounds.size(), base::SizeF(original.size()))) {
            auto placed = visual;
            const base::PointF origin = sample.bounds.topLeft() + snap;
            placed.outlinePath.translate(origin.x(), origin.y());
            paintShapeOutline(renderer, placed, base::AffineTransform(), sample.opacity);
            return;
        }
        base::AffineTransform transform;
        transform.translate(sample.bounds.x(), sample.bounds.y());
        transform.scale(sample.bounds.width() / original.width(),
                        sample.bounds.height() / original.height());
        transform.translate(snap.x(), snap.y());
        paintShapeOutline(renderer, visual, transform, sample.opacity);
        return;
    }
    auto outlined = visual.outlined();
    outlined.bounds.translate(snap);
    paintWindowOutline(renderer, transformedShape(outlined, original, sample), visual,
                       sample.opacity);
}

base::Rect WindowAnimation::absolutePaintBounds() const
{
    base::Rect bounds = absoluteGeometry();
    if (!paintsSubtree())
        return bounds;
    for (const auto *child : children()) {
        if (child->isVisible())
            bounds = bounds.united(subtreeBounds(child));
    }
    if (!animating())
        return bounds.united(m_coverage);
    return bounds.united(m_coverage)
            .united(m_oldCoverage)
            .united(m_presented.bounds.toEnclosingRect())
            .united(m_from.toEnclosingRect())
            .united(m_to.toEnclosingRect());
}

base::Rect WindowAnimation::textureCoverage() const
{
    base::Rect coverage = absoluteGeometry();
    for (const auto *child : children()) {
        if (child->isVisible())
            coverage = coverage.united(subtreeBounds(child));
    }
    return coverage.adjusted(-64, -64, 64, 64);
}

bool WindowAnimation::textureReusable(const base::Rect &coverage, const base::Rect &viewport,
                                      double scale) const
{
    if (!m_textureReusable || !m_texture || animating() || coverage.size() != m_coverage.size()
        || scale != m_textureScale || viewport != m_textureViewport
        || sceneRoot(*this).revision() != m_textureRootRevision || hasPromotedSurface(*this))
        return false;
    // Only whole device pixel moves keep the texture: it is rasterized at the origin's phase.
    const base::PointF moved = base::PointF(coverage.topLeft() - m_coverage.topLeft()) * scale;
    return base::fuzzyIsNull(moved.x() - std::round(moved.x()))
            && base::fuzzyIsNull(moved.y() - std::round(moved.y()));
}

bool WindowAnimation::redrawsSubtree(const base::Rect &viewport, double scale) const
{
    if (!isVisible() || m_closing || m_type == Type::Minimize)
        return false;
    const base::Rect coverage = textureCoverage();
    if (!animating() && !viewport.isEmpty() && !coverage.intersects(viewport))
        return false;
    if (textureReusable(coverage, viewport, scale))
        return false;
    return !drawsDirectly(m_textureDamage.hasDamage(*this, coverage, scale));
}

bool WindowAnimation::settled() const
{
    return !animating() && m_type != Type::Minimize && m_opacity >= 1 && !presentedScaled()
            && !m_presentedAt;
}

bool WindowAnimation::drawsDirectly(bool contentChanged) const
{
    if (m_style == Style::Popup)
        return false;
    const double period = m_framePeriod > 0 ? m_framePeriod : 1000.0 / 60;
    return contentChanged && m_contentChanged && m_directFrames < kDirectRefresh - 1
            && m_now() - m_textureDrawnAt < (kDirectRefresh - 0.5) * period && settled()
            && hasOpaqueSurface();
}

bool WindowAnimation::hasOpaqueSurface() const
{
    const std::function<bool(const Node &)> visit = [&](const Node &node) {
        for (const Node *child : node.children()) {
            const auto *surface = dynamic_cast<const SurfaceNode *>(child);
            if ((surface && surface->isOpaque()) || visit(*child))
                return true;
        }
        return false;
    };
    return visit(*this);
}

bool WindowAnimation::renderSiblingsBelow(render::Renderer &renderer, size_t begin,
                                          size_t end) const
{
    if (m_style != Style::Launchpad || !animating() || (!isVisible() && !paintsWhenHidden())) {
        m_belowTextures.clear();
        return false;
    }
    const base::Rect viewport = outputViewport(renderer, *this);
    if (renderer.capturing || renderer.suppressBackgroundBlur
        || !absolutePaintBounds().intersects(viewport))
        return false;
    const auto &siblings = parent()->children();
    for (size_t i = begin; i < end; ++i) {
        const auto *animation = dynamic_cast<const WindowAnimation *>(siblings[i]);
        if ((animation && animation->style() == Style::Launchpad)
            || hasPromotedSurface(*siblings[i]))
            return false;
    }
    const double scale = renderer.rasterScale();
    BelowTexture &below = m_belowTextures[renderer.outputContext().outputId];
    below.damage.setChildRange(begin, end);
    const bool reusable = below.texture && below.scale == scale && below.viewport == viewport
            && below.rootRevision == sceneRoot(*this).revision();
    if (reusable && m_type == Type::Open && !m_closing) {
        if (!below.damage.hasDamage(*parent(), viewport, scale)) {
            for (size_t i = begin; i < end; ++i)
                recordPresentation(renderer, *siblings[i]);
        }
        renderer.drawTexture(below.texture.get(), viewport);
        return true;
    }
    const base::Region damage = below.damage.update(*parent(), viewport, scale);
    if (reusable && damage.isEmpty()) {
        for (size_t i = begin; i < end; ++i)
            recordPresentation(renderer, *siblings[i]);
    } else {
        // The pass suspends presentation recording; what it draws shows this frame.
        render::PresentationSubmission *submission = renderer.presentationSubmission;
        const auto scope = uint64_t(reinterpret_cast<uintptr_t>(&below));
        const auto kept = [&](uint64_t owner) {
            return reusable && renderer.keepsBackdrop(scope, owner);
        };
        const base::Rect repaint = reusable
                ? render::coverSamples(damage.boundingRect(), below.damage.samples(kept))
                : base::Rect();
        const std::vector<render::KeptBackdrop> fresh = below.damage.freshBackdrops(kept);
        if (!renderer.renderToTexture(below.texture, viewport, repaint, [&] {
                renderer.presentationSubmission = submission;
                const render::BackdropPass backdrops(renderer, scope, fresh);
                for (size_t i = begin; i < end; ++i)
                    siblings[i]->render(renderer);
            })) {
            m_belowTextures.clear();
            return false;
        }
        below.scale = scale;
        below.viewport = viewport;
        below.rootRevision = sceneRoot(*this).revision();
        below.texture->label = "launchpad underlay";
    }
    renderer.drawTexture(below.texture.get(), viewport);
    return true;
}

bool WindowAnimation::textureIsCurrent(render::Renderer &renderer, const base::Rect &coverage,
                                       base::Rect *repaint) const
{
    const bool blurSuppressed =
            renderer.suppressBackgroundBlur || presentedScaled() || m_presentedAt;
    const uint64_t scope = renderer.backdropScope;
    const auto kept = [&](uint64_t owner) {
        return renderer.keptBackdropChange(scope, owner).has_value();
    };
    const base::Region damage = blurSuppressed
            ? m_textureDamage.update(*this, coverage, renderer.rasterScale())
            : m_textureDamage.update(*this, coverage, renderer.rasterScale(), 0,
                                     [&](uint64_t owner, const base::Rect &sampled) {
                                         if (const auto change = renderer.keptBackdropChange(scope, owner))
                                             return *change;
                                         return renderer.repaints(sampled) ? sampled : base::Rect();
                                     });
    const bool valid =
            textureReusable(coverage, outputViewport(renderer, *this), renderer.rasterScale());
    *repaint = !valid ? base::Rect()
            : blurSuppressed
            ? damage.boundingRect()
            : render::coverSamples(damage.boundingRect(), m_textureDamage.samples(kept));
    return valid && damage.isEmpty()
            && (m_textureBlurSuppressed == blurSuppressed || !hasBackgroundBlurs());
}

void WindowAnimation::swapStashedTexture(bool blurSuppressed) const
{
    const bool wanted = m_stashedTexture && m_stashedTexture->blurSuppressed == blurSuppressed;
    const bool other = m_texture && m_textureBlurSuppressed != blurSuppressed;
    if (!wanted && !(other && hasBackgroundBlurs()))
        return;
    if (!m_stashedTexture)
        m_stashedTexture = std::make_unique<StashedTexture>();
    StashedTexture &stash = *m_stashedTexture;
    std::swap(m_texture, stash.texture);
    std::swap(m_textureBlurSuppressed, stash.blurSuppressed);
    std::swap(m_textureMipmapped, stash.mipmapped);
    std::swap(m_textureDamage, stash.damage);
    std::swap(m_textureReusable, stash.reusable);
    std::swap(m_textureScale, stash.scale);
    std::swap(m_textureViewport, stash.viewport);
    std::swap(m_textureRootRevision, stash.rootRevision);
    std::swap(m_coverage, stash.coverage);
    std::swap(m_textureBounds, stash.bounds);
    if (!stash.texture)
        m_stashedTexture.reset();
}

bool WindowAnimation::hasBackgroundBlurs() const
{
    const std::function<bool(const Node &)> visit = [&](const Node &node) {
        for (const Node *child : node.children()) {
            const auto *surface = dynamic_cast<const SurfaceNode *>(child);
            if ((surface && surface->hasBackgroundBlur()) || visit(*child))
                return true;
        }
        return false;
    };
    return visit(*this);
}

void WindowAnimation::renderPresented(render::Renderer &renderer,
                                      const Presentation &presentation) const
{
    m_presentedAt = presentation;
    render(renderer);
    m_presentedAt.reset();
}

render::RoundedShape WindowAnimation::presentedBody(const Presentation &presentation) const
{
    return presentedBody(presentation, presentation.bounds);
}

render::RoundedShape WindowAnimation::presentedBody(const Presentation &presentation,
                                                    const base::RectF &bounds) const
{
    const base::Rect original = m_textureBounds.isEmpty() ? absoluteGeometry() : m_textureBounds;
    render::RoundedShape body = bodyShape(original, bounds);
    const double own = body.radius * std::min(body.radiusScale.width(), body.radiusScale.height());
    const double uniform = std::clamp(presentation.radius, double(0),
                                     std::min(body.bounds.width(), body.bounds.height()) / 2);
    body.radius = own + (uniform - own) * std::clamp(presentation.uniform, 0.0, 1.0);
    body.radiusScale = base::SizeF(1, 1);
    return body;
}

void WindowAnimation::drawPresented(render::Renderer &renderer, const Sample &value) const
{
    renderer.pushRoundedClip(presentedBody(*m_presentedAt, value.bounds));
    drawPresentedBlurs(renderer, value);
    draw(renderer, m_texture.get(), m_coverage, m_textureBounds, value);
    renderer.popRoundedClip();
    Sample outlined = value;
    outlined.opacity *= float(1 - std::clamp(m_presentedAt->uniform, 0.0, 1.0));
    if (outlined.opacity > 0)
        outline(renderer, m_visual, m_textureBounds, outlined);
}

void WindowAnimation::drawPresentedBlurs(render::Renderer &renderer, const Sample &value) const
{
    if (renderer.suppressBackgroundBlur || m_textureBounds.isEmpty())
        return;
    const double sx = value.bounds.width() / m_textureBounds.width();
    const double sy = value.bounds.height() / m_textureBounds.height();
    const double scale = std::min(sx, sy);
    base::AffineTransform transform;
    transform.translate(value.bounds.x(), value.bounds.y());
    transform.scale(sx, sy);
    transform.translate(-m_textureBounds.x(), -m_textureBounds.y());
    const bool reveal = isPresentedHidden();
    const std::function<void(const Node &)> visit = [&](const Node &node) {
        for (const Node *child : node.children()) {
            const auto *surface = dynamic_cast<const SurfaceNode *>(child);
            if (!child->isVisible() && !(reveal && surface && surface->isHiddenWithWindow()))
                continue;
            if (surface && surface->hasBackgroundBlur() && surface->texture()) {
                const base::Rect geometry = surface->absoluteGeometry();
                render::GlassParams glass = surface->glass();
                glass.blurSigma *= float(scale);
                glass.bevel *= float(scale);
                glass.depth *= float(scale);
                glass.displacement *= float(scale);
                glass.ambientOcclusionRadius *= float(scale);
                glass.plainSigma *= float(scale);
                glass.opacity *= value.opacity;
                const ui::Path &shape = surface->backgroundBlurShape();
                renderer.drawBackgroundBlur(
                        transform.mapRect(base::RectF(surface->backgroundBlurRect().translated(
                                                  geometry.topLeft())))
                                .toEnclosingRect(),
                        base::roundToInt(surface->backgroundBlurRadius() * scale),
                        transform.mapRect(base::RectF(geometry)).toEnclosingRect(), glass,
                        shape.isEmpty() ? ui::Path()
                                        : shape.translated(geometry.x(), geometry.y()).transformed(transform));
            }
            visit(*child);
        }
    };
    visit(*this);
}

void WindowAnimation::render(render::Renderer &renderer) const
{
    if ((!isVisible() && m_type != Type::Minimize && !m_closing && !m_presentedAt) || isRetired())
        return;
    // Sampled per output frame, not per draw.
    m_frameTime = renderer.frameTime;
    m_framePeriod = renderer.framePeriod;
    if (m_firstFrame) {
        auto *self = const_cast<WindowAnimation *>(this);
        const base::Rect geometry = absoluteGeometry();
        self->start(Type::Open, geometry, geometry, false);
        if (m_handoff) {
            const base::RectF &relative = m_handoff->bounds;
            self->m_from = base::RectF(geometry.x() + relative.x() * geometry.width(),
                                  geometry.y() + relative.y() * geometry.height(),
                                  relative.width() * geometry.width(),
                                  relative.height() * geometry.height());
            self->m_startOpacity = m_handoff->opacity;
            self->m_presented = {m_from, m_startOpacity, 0};
            self->m_handoff.reset();
        }
    }
    // A configure acknowledgement alone does not replace the old client buffer.
    if (m_running && isCrossFade(m_type) && !m_boundsContentReady && m_oldTexture) {
        Sample held = m_presented;
        held.opacity *= m_opacity;
        draw(renderer, m_oldTexture.get(), m_oldCoverage, m_oldBounds, held);
        outline(renderer, m_oldVisual, m_oldBounds, held);
        return;
    }
    if (m_running && m_started < 0) {
        m_firstStepAt = now();
        const bool leadIn =
                (m_style == Style::Window && (m_type == Type::Open || m_type == Type::Close))
                || (m_style == Style::Launchpad && m_type == Type::Close);
        m_started = leadIn ? m_firstStepAt - renderer.framePeriod : m_firstStepAt;
        m_firstStep = true;
    } else if (m_running && std::exchange(m_firstStep, false) && renderer.framePeriod > 0
               && now() - m_firstStepAt > renderer.framePeriod) {
        m_started += now() - m_firstStepAt - renderer.framePeriod;
    }

    const bool frozen = (m_closing || m_type == Type::Minimize) && !isPresentedHidden();
    if (frozen && !m_texture) {
        m_snapshot = nullptr;
        m_landingSnapshot = nullptr;
        m_holding = false;
        if (std::exchange(m_running, false) && m_finished)
            m_finished();
        return;
    }
    if (!frozen) {
        for (const Node *child : children()) {
            if (const auto *visual = dynamic_cast<const WindowVisualNode *>(child))
                m_visual = visual->state();
        }
        const base::Rect coverage = textureCoverage();
        const base::Rect viewport = renderer.outputContext().logicalViewport;
        if (!animating() && !m_presentedAt && !viewport.isEmpty() && !coverage.intersects(viewport))
            return;
        // In texture space; the draw below scales them to morphedRadius().
        std::optional<render::Renderer::WindowCorners> corners;
        if (morphsExterior()) {
            const Sample sample = current();
            const base::Rect geometry = absoluteGeometry();
            if (!sample.bounds.isEmpty()) {
                corners = render::Renderer::WindowCorners {
                        morphedRadius(sample),
                        base::SizeF(geometry.width() / sample.bounds.width(),
                               geometry.height() / sample.bounds.height())};
            }
        }
        // The pass suspends presentation recording, but this snapshot is shown in the same frame.
        render::PresentationSubmission *submission = renderer.presentationSubmission;
        const bool blurSuppressed =
                renderer.suppressBackgroundBlur || presentedScaled() || m_presentedAt;
        swapStashedTexture(blurSuppressed);
        const bool contentChanged =
                m_textureDamage.hasDamage(*this, coverage, renderer.rasterScale());
        const bool direct = !renderer.suppressBackgroundBlur && drawsDirectly(contentChanged);
        if (!renderer.capturing
            && (renderer.frameTime < 0 || renderer.frameTime != m_directFrameTime)) {
            m_directFrameTime = renderer.frameTime;
            m_directFrames = direct ? m_directFrames + 1 : 0;
            if (!direct)
                m_textureDrawnAt = m_now();
            m_contentChanged = contentChanged && settled() && m_texture;
        }
        base::Rect repaint;
        if (textureIsCurrent(renderer, coverage, &repaint)) {
            recordPresentation(renderer, *this);
        } else if (direct) {
            m_textureReusable = false;
            Node::render(renderer);
            m_presented = {base::RectF(absoluteGeometry()), 1, 1};
            return;
        } else {
            const bool reusable = !animating() && !hasPromotedSurface(*this);
            const uint64_t backdropScope = renderer.backdropScope;
            if (!renderer.renderToTexture(m_texture, coverage, repaint, [&] {
                    renderer.backdropScope = backdropScope;
                    const bool suppressed = renderer.suppressWindowOutlines;
                    const bool wasBlurSuppressed = renderer.suppressBackgroundBlur;
                    const bool revealed = renderer.revealHiddenSurfaces;
                    renderer.suppressWindowOutlines = true;
                    renderer.suppressBackgroundBlur = blurSuppressed;
                    renderer.revealHiddenSurfaces = isPresentedHidden();
                    renderer.presentationSubmission = submission;
                    renderer.windowCorners = corners;
                    renderSubtree(renderer);
                    renderer.windowCorners.reset();
                    renderer.revealHiddenSurfaces = revealed;
                    renderer.suppressBackgroundBlur = wasBlurSuppressed;
                    renderer.suppressWindowOutlines = suppressed;
                })) {
                m_textureReusable = false;
                Node::render(renderer);
                if (std::exchange(m_running, false) && m_finished)
                    m_finished();
                return;
            }
            m_texture->label = "window snapshot";
            const base::Rect drawn = repaint.isEmpty() ? coverage : repaint;
            const base::Rect output = outputViewport(renderer, *this);
            const auto &samples = m_textureDamage.samples();
            const bool backdropCurrent = blurSuppressed
                    || std::all_of(samples.begin(), samples.end(), [&](const base::Rect &sampled) {
                        return !sampled.intersects(drawn) || !sampled.intersects(output)
                                || renderer.repaints(sampled);
                    });
            m_textureReusable = reusable && backdropCurrent;
            m_textureBlurSuppressed = blurSuppressed;
            m_textureMipmapped = false;
            m_textureScale = renderer.rasterScale();
            m_textureViewport = output;
            m_textureRootRevision = sceneRoot(*this).revision();
        }
        m_coverage = coverage;
        m_textureBounds = absoluteGeometry();
        if (m_style == Style::Popup && m_running) {
            auto *self = const_cast<WindowAnimation *>(this);
            self->m_from = self->m_to = base::RectF(m_textureBounds);
        }
    }

    if (m_running && m_type == Type::Minimize)
        renderer.enableTrilinearFiltering(m_texture.get());
    if (m_snapshot && m_type == Type::Minimize && !m_presentedAt)
        readSnapshot(renderer);

    Sample value = (m_running || m_holding) && !isPresentedHidden()
            ? current()
            : Sample {absoluteGeometry(), 1, 1};
    if (!m_presentedAt)
        m_presented = value;
    value.opacity *= m_opacity;
    if (m_scaleFrom != m_scaleTo && m_scaleStarted < 0)
        m_scaleStarted = now();
    if (const double scale = presentationScale(); scale != 1) {
        const base::Rect geometry = absoluteGeometry();
        const base::PointF anchor(geometry.x() + std::clamp<double>(m_scaleAnchor.x(), 0, geometry.width()),
                             geometry.y() + std::clamp<double>(m_scaleAnchor.y(), 0, geometry.height()));
        value.bounds = base::RectF(anchor + (value.bounds.topLeft() - anchor) * scale,
                              value.bounds.size() * scale);
    }
    if (m_presentedAt) {
        const base::Rect geometry = absoluteGeometry();
        if (!geometry.isEmpty()) {
            const base::RectF &to = m_presentedAt->bounds;
            const double sx = to.width() / geometry.width();
            const double sy = to.height() / geometry.height();
            const base::PointF from = value.bounds.topLeft() - base::PointF(geometry.topLeft());
            value.bounds = base::RectF(to.topLeft() + base::PointF(from.x() * sx, from.y() * sy),
                                  base::SizeF(value.bounds.width() * sx, value.bounds.height() * sy));
            if (m_texture && !m_textureMipmapped && std::min(sx, sy) < 0.75) {
                renderer.enableTrilinearFiltering(m_texture.get());
                m_textureMipmapped = true;
            }
        }
        value.opacity *= m_presentedAt->opacity;
    }
    if (m_running && m_type == Type::Minimize && value.progress >= 1 && m_landingSnapshot)
        readSnapshot(renderer, true);

    if ((m_running || m_holding) && m_type == Type::Minimize && value.progress >= 1
        && m_landingTexture && !isPresentedHidden()) {
        renderer.drawTextureF(m_landingTexture.get(), value.bounds, value.opacity);
    } else if (!frozen || m_running || m_holding) {
        const double dissolved = std::min(value.progress / kCrossFadeContentShare, 1.0);
        const float blend = m_running && isCrossFade(m_type) ? 1 - std::pow(1 - dissolved, 2) : 1;
        const float exterior = exteriorOpacity(value);
        if (m_presentedAt) {
            drawPresented(renderer, value);
        } else if (morphsExterior()) {
            drawExteriorMorph(renderer, value, blend);
        } else {
            if (exterior < 1) {
                if (exterior > 0)
                    draw(renderer, m_texture.get(), m_coverage, m_textureBounds, value, exterior);
                renderer.pushRoundedClip(bodyShape(m_textureBounds, value.bounds));
                draw(renderer, m_texture.get(), m_coverage, m_textureBounds, value);
                renderer.popRoundedClip();
            } else {
                draw(renderer, m_texture.get(), m_coverage, m_textureBounds, value);
            }
            if (m_oldTexture && blend < 1) {
                if (m_visual.clipContent)
                    renderer.pushRoundedClip(
                            transformedShape(m_visual.shape, m_textureBounds, value));
                draw(renderer, m_oldTexture.get(), m_oldCoverage, m_oldBounds, value, 1 - blend);
                if (m_visual.clipContent)
                    renderer.popRoundedClip();
            }
            Sample outlined = value;
            outlined.opacity *= exterior;
            outline(renderer, m_visual, m_textureBounds, outlined);
        }
    }
    if (m_scaleFrom != m_scaleTo) {
        if (presentationScale() == m_scaleTo)
            m_scaleFrom = m_scaleTo;
        changed();
    }
    if (m_running) {
        if (value.progress >= 1) {
            m_running = false;
            m_holding = m_holdAtEnd && m_type == Type::Minimize;
            m_oldTexture.reset();
            if (m_finished)
                m_finished();
        }
        changed();
    }
}

} // namespace cutefish::scene
