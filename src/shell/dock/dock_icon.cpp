// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/dock/dock_icon.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "core/main_loop.h"
#include "shell/common/count_badge.h"
#include "shell/dock/dock.h"
#include "shell/dock/dock_metrics.h"
#include "shell/dock/dock_presentation.h"
#include "shell/dock/dock_window.h"
#include "shell/shell_window.h"
#include "ui/animation.h"
#include "ui/embedded_window.h"
#include "ui/icon.h"
#include "ui/painter.h"
#include "ui/theme.h"

namespace cutefish::shell {

// How long a press is held before it counts as press-and-hold.
static constexpr std::chrono::milliseconds kPressAndHold {800};
// How far a press moves before it starts a drag.
static constexpr double kStartDragDistance = 10;
// A launch bounces the whole hop whether or not the application is up sooner:
// a quick rise that hangs at the top, then a fall that speeds up and eases
// into the landing.
static constexpr double kBounceRiseMs = 300;
static constexpr double kBounceFallMs = 370;
// The fall's timing curve, a cubic Bezier from (0, 0) to (1, 1).
static constexpr double kFallX1 = 0.5;
static constexpr double kFallY1 = 0;
static constexpr double kFallX2 = 0.3;
static constexpr double kFallY2 = 1;

// The cubic Bezier timing curve through (x1, y1) and (x2, y2), at `x`.
static double cubicBezier(double x1, double y1, double x2, double y2, double x)
{
    const auto at = [](double p1, double p2, double t) {
        const double u = 1 - t;
        return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
    };
    // x rises monotonically with the curve's parameter: bisect for it.
    double low = 0;
    double high = 1;
    for (int i = 0; i < 40; ++i) {
        const double middle = (low + high) / 2;
        (at(x1, x2, middle) < x ? low : high) = middle;
    }
    return at(y1, y2, (low + high) / 2);
}

// Of the bounce height, `ms` into the bounce; negative once it is over.
static double bounceLift(double ms)
{
    if (ms < kBounceRiseMs) {
        const double t = 1 - ms / kBounceRiseMs;
        return 1 - t * t * t;
    }
    ms -= kBounceRiseMs;
    if (ms < kBounceFallMs)
        return 1 - cubicBezier(kFallX1, kFallY1, kFallX2, kFallY2, ms / kBounceFallMs);
    return -1;
}

// The launch bounce, stepped on the dock's frames as its transitions are, the
// first frame already one interval in.
class DockIcon::Bounce final : public ui::FrameClient
{
public:
    explicit Bounce(DockIcon *icon)
        : FrameClient(icon->m_dock->window()->window())
        , m_icon(icon)
    {
    }

    void start()
    {
        if (isTicking())
            return;
        m_startMs = -1;
        startTicking();
    }

    bool isRunning() const { return isTicking(); }

protected:
    void tick(uint64_t frameTimeNs) override
    {
        const double nowMs = double(frameTimeNs) / 1e6;
        if (m_startMs < 0)
            m_startMs = nowMs - DockPresentation::kFrameIntervalMs;
        const double lift = bounceLift(nowMs - m_startMs);
        if (lift < 0) {
            stopTicking();
            m_icon->setLift(0);
            return;
        }
        m_icon->setLift(lift);
    }

private:
    DockIcon *m_icon;
    double m_startMs = -1;
};

// The launchpad's artwork, an SVG drawn at the icon's device size as
// FishUI's IconItem renders a file.
class DockIcon::SvgFace final : public ui::View
{
public:
    void setFile(std::string file)
    {
        m_file = std::move(file);
        requestPaint();
    }

    void setTint(std::optional<ui::Color> tint)
    {
        m_tint = tint;
        requestPaint();
    }

protected:
    void paint(ui::Painter &painter) override
    {
        if (m_file.empty())
            return;
        // On whole device pixels, as FishUI's IconItem puts its pixmap.
        const ui::Rect device = painter.snappedRect(localRect());
        const double scale = painter.scale();
        const ui::PointF offset = painter.offset();
        painter.drawSvg(m_file, {device.x / scale - offset.x, device.y / scale - offset.y,
                                 device.width / scale, device.height / scale},
                        m_tint);
    }

private:
    std::string m_file;
    std::optional<ui::Color> m_tint;
};

// A minimized window's image, fitted into the view.
class DockIcon::ThumbnailFace final : public ui::View
{
public:
    explicit ThumbnailFace(DockIcon *icon)
        : m_icon(icon)
    {
    }

protected:
    void paint(ui::Painter &painter) override { m_icon->paintThumbnail(painter, localRect()); }

private:
    DockIcon *m_icon;
};

// A pressed thumbnail: its fitted shape and the badge in black, as one layer
// at 0.5 so where they overlap is no darker.
class DockIcon::ThumbnailMask final : public ui::View
{
public:
    ThumbnailMask()
    {
        m_badge = addChild<ui::ThemeIcon>(std::string(), 16);
        m_badge->setColor(ui::Color {0, 0, 0, 255});
        setOpacity(0.5);
        setLayered(true);
    }

    void update(const ui::RectF &shape, double radius, const std::string &icon, const ui::RectF &badge,
                double badgeOpacity)
    {
        m_shape = shape;
        m_radius = radius;
        m_badge->setIconName(icon);
        m_badge->setSize(badge.width);
        m_badge->setGeometry(badge);
        m_badge->setOpacity(badgeOpacity);
        requestPaint();
    }

protected:
    void paint(ui::Painter &painter) override
    {
        painter.drawSmoothRectangle(m_shape, m_radius, ui::Color {0, 0, 0, 255});
    }

private:
    ui::ThemeIcon *m_badge = nullptr;
    ui::RectF m_shape;
    double m_radius = 0;
};

DockIcon::DockIcon(DockWindow *dock, Kind kind)
    : m_dock(dock)
    , m_kind(kind)
    , m_holdTimer(core::mainLoop())
{
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(kPressAndHold);
    m_holdTimer.onTimeout = [this] {
        if (!m_pressed || m_pressedButton != int(ui::MouseButton::Left))
            return;
        // MouseArea drops the click that would follow the release.
        m_held = true;
        m_dock->iconMenuRequested(this);
    };
}

DockIcon::~DockIcon() = default;

void DockIcon::setDragHidden(bool hidden)
{
    if (hidden == m_dragHidden)
        return;
    m_dragHidden = hidden;
    updateFace();
    requestPaint();
}

void DockIcon::setDropHover(bool hover)
{
    if (hover == m_dropHover)
        return;
    m_dropHover = hover;
    updateMask();
}

void DockIcon::releasePress()
{
    m_holdTimer.stop();
    m_pressed = false;
    m_pressedButton = -1;
    m_dragArmed = false;
    updateMask();
}

void DockIcon::setIcon(std::string name, bool file)
{
    if (name == m_iconName && file == m_iconIsFile)
        return;
    m_iconName = std::move(name);
    m_iconIsFile = file;
    updateFace();
}

void DockIcon::updateFace()
{
    if (m_iconIsFile) {
        if (m_themeFace)
            m_themeFace->setVisible(false);
        if (!m_svgFace)
            m_svgFace = addChild<SvgFace>();
        m_svgFace->setVisible(m_thumbnail.isNull() && !m_dragHidden);
        m_svgFace->setFile(m_iconName);
    } else {
        if (m_svgFace)
            m_svgFace->setVisible(false);
        if (!m_themeFace)
            m_themeFace = addChild<ui::ThemeIcon>(m_iconName, m_iconRect.width);
        m_themeFace->setVisible(m_thumbnail.isNull() && !m_dragHidden);
        m_themeFace->setIconName(m_iconName);
    }
    if (m_thumbnailFace)
        m_thumbnailFace->setVisible(!m_thumbnail.isNull() && !m_dragHidden);
    if (m_badge) {
        m_badge->setIconName(m_iconName);
        m_badge->setVisible(!m_thumbnail.isNull() && !m_dragHidden);
    }
    if (m_themeMask)
        m_themeMask->setIconName(m_iconName);
    if (m_svgMask)
        m_svgMask->setFile(m_iconName);
    setIconRect(m_iconRect);
    updateMask();
}

void DockIcon::updateMask()
{
    // Made on the first press, then only hidden, never destroyed. The
    // trash dims the same way while a drag hovers it.
    const bool dimmed = (isDimmed() || m_dropHover) && m_thumbnail.isNull();
    if (dimmed && m_iconIsFile && !m_svgMask) {
        m_svgMask = addChild<SvgFace>();
        m_svgMask->setFile(m_iconName);
        m_svgMask->setTint(ui::Color {0, 0, 0, 255});
        setIconRect(m_iconRect);
    } else if (dimmed && !m_iconIsFile && !m_themeMask) {
        m_themeMask = addChild<ui::ThemeIcon>(m_iconName, m_iconRect.width);
        m_themeMask->setColor(ui::Color {0, 0, 0, 255});
        setIconRect(m_iconRect);
    }
    if (m_themeMask)
        m_themeMask->setVisible(dimmed && !m_iconIsFile);
    if (m_svgMask)
        m_svgMask->setVisible(dimmed && m_iconIsFile);

    const bool thumbnailDimmed = isDimmed() && !m_thumbnail.isNull();
    if (thumbnailDimmed && !m_thumbnailMask) {
        m_thumbnailMask = addChild<ThumbnailMask>();
        setIconRect(m_iconRect);
    }
    if (m_thumbnailMask)
        m_thumbnailMask->setVisible(thumbnailDimmed);
}

void DockIcon::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    requestPaint();
}

void DockIcon::setThumbnail(const ui::Bitmap *thumbnail, double aspect)
{
    m_thumbnailAspect = aspect > 0 ? aspect : 1;
    if (!thumbnail || thumbnail->isNull()) {
        m_thumbnail = ui::Image();
    } else {
        // Unpremultiplied 0xAARRGGBB, as Painter::drawImage takes it.
        const ui::Bitmap rgba = thumbnail->converted(ui::Bitmap::Format::Rgba8888Premultiplied);
        m_thumbnail = ui::Image(rgba.width(), rgba.height());
        for (int y = 0; y < rgba.height(); ++y) {
            const uint8_t *line = rgba.constScanLine(y);
            uint32_t *out = m_thumbnail.bits() + size_t(y) * rgba.width();
            for (int x = 0; x < rgba.width(); ++x) {
                const uint8_t *pixel = line + size_t(x) * 4;
                const uint32_t a = pixel[3];
                const auto channel = [a](uint32_t value) {
                    return a ? std::min<uint32_t>(255, (value * 255 + a / 2) / a) : 0;
                };
                out[x] = a << 24 | channel(pixel[0]) << 16 | channel(pixel[1]) << 8 | channel(pixel[2]);
            }
        }
    }
    // Under the badge.
    if (!m_thumbnail.isNull() && !m_thumbnailFace)
        m_thumbnailFace = addChild<ThumbnailFace>(this);
    if (!m_thumbnail.isNull() && !m_badge)
        m_badge = addChild<ui::ThemeIcon>(m_iconName, 16);
    if (m_thumbnailFace)
        m_thumbnailFace->requestPaint();
    updateFace();
    requestPaint();
}

void DockIcon::setBadgeOpacity(double opacity)
{
    if (m_badgeSet && opacity == m_badgeTarget)
        return;
    m_badgeTarget = opacity;
    if (!m_badgeSet) {
        // The first value is set at once, not animated.
        m_badgeSet = true;
        m_badgeOpacity = opacity;
        applyBadgeOpacity();
        return;
    }
    if (!m_badgeFade) {
        m_badgeFade = std::make_unique<ui::Animation>(m_dock->window()->window());
        m_badgeFade->setDuration(std::chrono::milliseconds(140));
        m_badgeFade->setEasing(ui::Animation::Easing::OutCubic);
        m_badgeFade->onUpdate = [this](double value) {
            m_badgeOpacity = m_badgeFrom + (m_badgeTarget - m_badgeFrom) * value;
            applyBadgeOpacity();
        };
    }
    m_badgeFade->stop();
    m_badgeFrom = m_badgeOpacity;
    m_badgeFade->start();
}

void DockIcon::applyBadgeOpacity()
{
    if (m_badge)
        m_badge->setOpacity(m_badgeOpacity * m_opacity);
    if (m_thumbnailMask)
        setIconRect(m_iconRect);
}

void DockIcon::setIconRect(const ui::RectF &rect)
{
    m_iconRect = rect;
    const ui::RectF art = artworkRect();
    // From the artwork's size to the magnified icon's; exactly 1 at rest.
    const double magnified = magnifiedIconRect().width / art.width;
    const ui::Transform transform = artworkTransform(art, m_scale * magnified);
    for (ui::View *face : {static_cast<ui::View *>(m_themeFace), static_cast<ui::View *>(m_svgFace),
                           static_cast<ui::View *>(m_themeMask), static_cast<ui::View *>(m_svgMask)}) {
        if (!face)
            continue;
        face->setGeometry(art);
        face->setTransform(transform);
        const bool mask = face == m_themeMask || face == m_svgMask;
        face->setOpacity(m_opacity * (mask ? 0.4 : 1));
    }
    if (m_themeFace)
        m_themeFace->setSize(art.width);
    if (m_themeMask)
        m_themeMask->setSize(art.width);
    if (m_thumbnailFace) {
        m_thumbnailFace->setGeometry(art);
        m_thumbnailFace->setTransform(artworkTransform(art, magnified));
        m_thumbnailFace->setOpacity(m_opacity);
    }
    if (m_badge) {
        const double size = std::round(art.width * 0.42);
        const ui::RectF badge {art.x + art.width - size, art.y + art.height - size, size, size};
        m_badge->setSize(size);
        m_badge->setGeometry(badge);
        m_badge->setTransform(artworkTransform(badge, magnified));
        m_badge->setOpacity(m_badgeOpacity * m_opacity);
        if (m_thumbnailMask) {
            const ui::RectF local {0, 0, art.width, art.height};
            m_thumbnailMask->setGeometry(art);
            m_thumbnailMask->setTransform(artworkTransform(art, magnified));
            m_thumbnailMask->update(thumbnailRect(local), std::max(2.0, art.width * 2 / 54), m_iconName,
                                    {badge.x - art.x, badge.y - art.y, size, size}, m_badgeOpacity);
        }
    }
    placeCountBadge();
    requestPaint();
}

void DockIcon::setMagnification(double scale, int rasterSize)
{
    if (scale == m_magnification && rasterSize == m_rasterSize)
        return;
    m_magnification = scale;
    m_rasterSize = rasterSize;
    setIconRect(m_iconRect);
}

ui::RectF DockIcon::magnifiedIconRect() const
{
    if (m_magnification == 1)
        return m_iconRect;
    const ui::RectF &rect = m_iconRect;
    const double width = rect.width * m_magnification;
    const double height = rect.height * m_magnification;
    const double centreX = rect.x + rect.width / 2;
    const double centreY = rect.y + rect.height / 2;
    switch (m_dock->dock().settings().direction()) {
    case DockSettings::Left:
        return {rect.x, centreY - height / 2, width, height};
    case DockSettings::Right:
        return {rect.x + rect.width - width, centreY - height / 2, width, height};
    case DockSettings::Bottom:
        break;
    }
    return {centreX - width / 2, rect.y + rect.height - height, width, height};
}

ui::RectF DockIcon::artworkRect() const
{
    if (m_rasterSize <= 0)
        return m_iconRect;
    const ui::RectF drawn = magnifiedIconRect();
    const double size = m_magnification > 1 ? std::max<double>(m_rasterSize, m_iconRect.width) : m_iconRect.width;
    // Around the magnified icon's centre, its top left on whole device
    // pixels in the window, where the artwork's raster lands exactly.
    const double ratio = m_dock->window()->scale();
    const ui::PointF origin = mapToWindow({0, 0});
    const auto snap = [&](double centre, double offset) {
        return std::round((offset + centre - size / 2) * ratio) / ratio - offset;
    };
    return {snap(drawn.x + drawn.width / 2, origin.x), snap(drawn.y + drawn.height / 2, origin.y), size, size};
}

ui::Transform DockIcon::artworkTransform(const ui::RectF &rect, double scale) const
{
    // ui::Transform scales about the view's own centre.
    const ui::RectF art = artworkRect();
    ui::Transform transform;
    transform.scale = scale;
    transform.translation = {(scale - 1) * (rect.x + rect.width / 2 - (art.x + art.width / 2)),
                             (scale - 1) * (rect.y + rect.height / 2 - (art.y + art.height / 2))};
    transform.translation += bounceOffset();
    if (m_magnification > 1) {
        // Magnified, the artwork's centre goes exactly to the icon's, which
        // a scale of exactly 1 would put on whole device pixels.
        const ui::RectF drawn = magnifiedIconRect();
        transform.translation += ui::PointF {drawn.x + drawn.width / 2 - (art.x + art.width / 2),
                                             drawn.y + drawn.height / 2 - (art.y + art.height / 2)};
        if (transform.scale == 1)
            transform.scale = std::nextafter(1.0, 0.0);
    }
    return transform;
}

void DockIcon::prepareMagnifiedSize(int size)
{
    const double scale = m_dock->window()->scale();
    if (size == m_preparedSize && scale == m_preparedScale && m_iconName == m_preparedName)
        return;
    m_preparedSize = size;
    m_preparedScale = scale;
    m_preparedName = m_iconName;
    if (size <= 0 || m_iconName.empty())
        return;
    if (!m_iconIsFile) {
        // The pressed look tints the same raster.
        ui::preloadThemeIcons({m_iconName}, size, scale);
        if (m_kind == Kind::Minimized)
            ui::preloadThemeIcons({m_iconName}, int(std::round(size * 0.42)), scale);
        return;
    }
    // Drawing the artwork once at that size caches its raster.
    for (const std::optional<ui::Color> tint : {std::optional<ui::Color>(), std::optional(ui::Color {0, 0, 0, 255})}) {
        SvgFace face;
        face.setFile(m_iconName);
        face.setTint(tint);
        face.setGeometry({0, 0, double(size), double(size)});
        face.grab(scale);
    }
}

void DockIcon::setRestGeometry(const ui::RectF &slot, const ui::RectF &icon)
{
    m_restGeometry = slot;
    m_restIconRect = icon;
}

void DockIcon::setCount(int64_t count)
{
    if (!m_countBadge) {
        if (count <= 0)
            return;
        m_countBadge = addChild<CountBadge>();
    }
    m_countBadge->setCount(count);
    placeCountBadge();
}

void DockIcon::placeCountBadge()
{
    if (!m_countBadge)
        return;
    raiseChild(m_countBadge);
    const ui::RectF art = artworkRect();
    m_countBadge->setIconRect(art);
    // Scaled with the icon about the icon's centre.
    m_countBadge->setTransform(
            artworkTransform(m_countBadge->geometry(), m_scale * magnifiedIconRect().width / art.width));
    m_countBadge->setOpacity(m_opacity);
    m_countBadge->setVisible(m_countBadge->count() > 0 && !m_dragHidden);
}

void DockIcon::setPresentation(double scale, double opacity)
{
    if (scale == m_scale && opacity == m_opacity)
        return;
    m_scale = scale;
    m_opacity = opacity;
    setIconRect(m_iconRect);
}

void DockIcon::setPresentationOpacity(double opacity)
{
    setPresentation(m_scale, opacity);
}

void DockIcon::bounce()
{
    if (!m_bounce)
        m_bounce = std::make_unique<Bounce>(this);
    m_bounce->start();
}

bool DockIcon::isBouncing() const
{
    return m_bounce && m_bounce->isRunning();
}

void DockIcon::setLift(double lift)
{
    if (lift == m_lift)
        return;
    m_lift = lift;
    setIconRect(m_iconRect);
    m_dock->iconBounced(this);
}

ui::PointF DockIcon::bounceOffset() const
{
    if (m_lift == 0)
        return {};
    const double ratio = m_dock->window()->scale();
    const double height = DockMetrics::forIconSize(int(std::lround(m_iconRect.width))).bounceHeight();
    const double lift = std::round(m_lift * height * ratio) / ratio;
    switch (m_dock->dock().settings().direction()) {
    case DockSettings::Left:
        return {lift, 0};
    case DockSettings::Right:
        return {-lift, 0};
    case DockSettings::Bottom:
        break;
    }
    return {0, -lift};
}

void DockIcon::setDotRect(const ui::RectF &rect)
{
    m_dotRect = rect;
    requestPaint();
}

void DockIcon::setMenuOpen(bool open)
{
    if (open == m_menuOpen)
        return;
    m_menuOpen = open;
    updateMask();
}

void DockIcon::pointerEnter()
{
    m_hovered = true;
    m_dock->iconHovered(this, true);
}

void DockIcon::pointerLeave()
{
    m_hovered = false;
    m_dock->iconHovered(this, false);
}

void DockIcon::pointerPress(const ui::PointerEvent &event)
{
    m_pressed = true;
    m_held = false;
    m_pressedButton = int(event.button);
    if (event.button == ui::MouseButton::Left)
        m_holdTimer.start();
    m_dragArmed = m_draggable && event.button == ui::MouseButton::Left
            && event.device != ui::PointerDevice::TouchScreen;
    m_pressPoint = event.position;
    updateMask();
}

void DockIcon::pointerMove(const ui::PointerEvent &event)
{
    if (!m_dragArmed || !m_pressed)
        return;
    const double dx = event.position.x - m_pressPoint.x;
    const double dy = event.position.y - m_pressPoint.y;
    if (dx * dx + dy * dy < kStartDragDistance * kStartDragDistance)
        return;
    m_dragArmed = false;
    m_dock->iconDragRequested(this);
}

void DockIcon::pointerRelease(const ui::PointerEvent &event)
{
    m_holdTimer.stop();
    const bool inside = localRect().contains(event.position);
    const bool held = std::exchange(m_held, false);
    m_dragArmed = false;
    // A right click opens the menu from the click; the menu then holds the dim.
    if (inside && !held && event.button == ui::MouseButton::Right)
        m_dock->iconMenuRequested(this);
    // A right click hands the dim to the menu it opened.
    m_pressed = false;
    m_pressedButton = -1;
    updateMask();
    if (inside && !held && event.button != ui::MouseButton::Right)
        m_dock->iconClicked(this, int(event.button));
    // The press took the tooltip down with the window's popups; one still
    // hovering gets it back.
    if (m_hovered && !m_menuOpen)
        m_dock->iconHovered(this, true);
}

void DockIcon::paint(ui::Painter &painter)
{
    if (m_dragHidden)
        return;
    if (isDotShown() && m_dotRect.width > 0) {
        painter.drawSmoothRectangle(m_dotRect, m_dotRect.width / 2, ui::Theme::current().text);
    }
}

ui::RectF DockIcon::thumbnailRect(const ui::RectF &slot) const
{
    // Fitted the way the compositor fits the window landing here, to whole
    // pixels, so the two line up exactly.
    const bool wide = m_thumbnailAspect > slot.width / std::max(1.0, slot.height);
    const double fitWidth = wide ? slot.width : slot.height * m_thumbnailAspect;
    const double fitHeight = wide ? slot.width / m_thumbnailAspect : slot.height;
    const double x = std::max(0.0, std::floor((slot.width - fitWidth) / 2));
    const double y = std::max(0.0, std::floor((slot.height - fitHeight) / 2));
    const double width = std::min(slot.width, std::ceil((slot.width + fitWidth) / 2)) - x;
    const double height = std::min(slot.height, std::ceil((slot.height + fitHeight) / 2)) - y;
    return {slot.x + x, slot.y + y, width, height};
}

void DockIcon::paintThumbnail(ui::Painter &painter, const ui::RectF &slot)
{
    const ui::RectF rect = thumbnailRect(slot);
    const double radius = std::max(2.0, slot.width * 2 / 54);
    painter.save();
    painter.clipRoundedRect(rect, radius);
    painter.drawImage(m_thumbnail, rect);
    painter.restore();
}

} // namespace cutefish::shell
