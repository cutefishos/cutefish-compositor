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
    m_directBackdrop = false;
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
    m_directBackdrop = false;
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
    m_directBackdrop = false;
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
    m_directBackdrop = false;
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

} // namespace cutefish::scene
