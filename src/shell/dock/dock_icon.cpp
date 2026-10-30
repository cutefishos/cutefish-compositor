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

// The launcher's artwork, an SVG drawn at the icon's device size as
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
    if (!m_thumbnail.isNull() && !m_badge)
        m_badge = addChild<ui::ThemeIcon>(m_iconName, 16);
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
    const ui::Transform transform = [&] {
        ui::Transform result;
        // ui::Transform scales about the view's centre.
        result.scale = m_scale;
        result.translation = bounceOffset();
        return result;
    }();
    for (ui::View *face : {static_cast<ui::View *>(m_themeFace), static_cast<ui::View *>(m_svgFace),
                           static_cast<ui::View *>(m_themeMask), static_cast<ui::View *>(m_svgMask)}) {
        if (!face)
            continue;
        face->setGeometry(rect);
        face->setTransform(transform);
        const bool mask = face == m_themeMask || face == m_svgMask;
        face->setOpacity(m_opacity * (mask ? 0.4 : 1));
    }
    if (m_themeFace)
        m_themeFace->setSize(rect.width);
    if (m_themeMask)
        m_themeMask->setSize(rect.width);
    if (m_badge) {
        const double size = std::round(rect.width * 0.42);
        m_badge->setSize(size);
        m_badge->setGeometry({rect.x + rect.width - size, rect.y + rect.height - size, size, size});
        m_badge->setOpacity(m_badgeOpacity * m_opacity);
        if (m_thumbnailMask) {
            const ui::RectF shape = thumbnailRect();
            m_thumbnailMask->setGeometry(localRect());
            m_thumbnailMask->update(shape, std::max(2.0, rect.width * 2 / 54), m_iconName,
                                    {rect.x + rect.width - size, rect.y + rect.height - size, size, size},
                                    m_badgeOpacity);
        }
    }
    placeCountBadge();
    requestPaint();
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
    m_countBadge->setIconRect(m_iconRect);
    // Scaled with the icon about the icon's centre; ui::Transform scales
    // about the badge's own.
    const ui::RectF badge = m_countBadge->geometry();
    ui::Transform transform;
    transform.scale = m_scale;
    transform.translation = {(m_scale - 1) * (badge.x + badge.width / 2 - (m_iconRect.x + m_iconRect.width / 2)),
                             (m_scale - 1) * (badge.y + badge.height / 2 - (m_iconRect.y + m_iconRect.height / 2))};
    transform.translation += bounceOffset();
    m_countBadge->setTransform(transform);
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
    if (!m_thumbnail.isNull())
        paintThumbnail(painter);
    if (isDotShown() && m_dotRect.width > 0) {
        painter.drawSmoothRectangle(m_dotRect, m_dotRect.width / 2, ui::Theme::current().text);
    }
}

ui::RectF DockIcon::thumbnailRect() const
{
    // Fitted the way the compositor fits the window landing here, to whole
    // pixels, so the two line up exactly.
    const ui::RectF &slot = m_iconRect;
    const bool wide = m_thumbnailAspect > slot.width / std::max(1.0, slot.height);
    const double fitWidth = wide ? slot.width : slot.height * m_thumbnailAspect;
    const double fitHeight = wide ? slot.width / m_thumbnailAspect : slot.height;
    const double x = std::max(0.0, std::floor((slot.width - fitWidth) / 2));
    const double y = std::max(0.0, std::floor((slot.height - fitHeight) / 2));
    const double width = std::min(slot.width, std::ceil((slot.width + fitWidth) / 2)) - x;
    const double height = std::min(slot.height, std::ceil((slot.height + fitHeight) / 2)) - y;
    return {slot.x + x, slot.y + y, width, height};
}

void DockIcon::paintThumbnail(ui::Painter &painter)
{
    const ui::RectF &slot = m_iconRect;
    const ui::RectF rect = thumbnailRect();
    const double radius = std::max(2.0, slot.width * 2 / 54);
    painter.save();
    painter.multiplyOpacity(m_opacity);
    painter.clipRoundedRect(rect, radius);
    painter.drawImage(m_thumbnail, rect);
    painter.restore();
}

} // namespace cutefish::shell
