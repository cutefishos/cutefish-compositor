// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/dock/dock_window.h"

#include <algorithm>
#include <cmath>

#include "base/dbus.h"
#include "base/i18n.h"
#include "core/main_loop.h"
#include "input/drag.h"
#include "input/seat.h"
#include "render/renderer.h"
#include "shell/common/application_badges.h"
#include "shell/common/task_manager.h"
#include "shell/common/task_windows.h"
#include "shell/dock/dock.h"
#include "shell/dock/dock_icon.h"
#include "shell/dock/dock_presentation.h"
#include "shell/dock/dock_tooltip.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "toolkit/compositor_colors.h"
#include "ui/application.h"
#include "ui/embedded_window.h"
#include "ui/event.h"
#include "ui/frame_client.h"
#include "ui/menu.h"
#include "ui/painter.h"
#include "ui/theme.h"
#include "wm/window.h"
#include "wm/window_manager.h"

using cutefish::i18n::tr;

namespace cutefish::shell {

namespace {

constexpr double kPace[] = {0.000, 0.048, 0.116, 0.174, 0.249, 0.314, 0.361,
                            0.398, 0.435, 0.469, 0.500, 0.587, 0.630, 0.666,
                            0.701, 0.739, 0.781, 0.827, 0.879, 0.931, 1.000};

constexpr double kOpaqueLightShade = 1.06;
constexpr double kOpaqueBlurSigma = 30;
constexpr double kOpaqueTintAlpha = 0.7;

double glassAmount(double tint)
{
    const int last = int(std::size(kPace)) - 1;
    const double x = std::clamp(tint, 0.0, 1.0) * last;
    const int i = std::min(int(std::floor(x)), last - 1);
    return kPace[i] + (x - i) * (kPace[i + 1] - kPace[i]);
}

double glassBlend(double amount, double clearValue, double defaultValue, double tintedValue)
{
    const double d0 = 2.0 * (defaultValue - clearValue);
    const double d1 = 2.0 * (tintedValue - defaultValue);
    const double middle = d0 * d1 > 0 ? 2.0 * d0 * d1 / (d0 + d1) : 0.0;
    const bool upper = amount > 0.5;
    const double t = upper ? 2.0 * amount - 1.0 : 2.0 * amount;
    const double y0 = upper ? defaultValue : clearValue;
    const double y1 = upper ? tintedValue : defaultValue;
    const double m0 = 0.5 * (upper ? middle : d0);
    const double m1 = 0.5 * (upper ? d1 : middle);
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * y0 + (t3 - 2.0 * t2 + t) * m0
            + (3.0 * t2 - 2.0 * t3) * y1 + (t3 - t2) * m1;
}

// The fewest logical pixels that are also whole device pixels at `scale`,
// up to 8; 1 at a scale with none.
int deviceStep(double scale)
{
    for (int step = 1; step <= 8; ++step) {
        if (std::abs(step * scale - std::round(step * scale)) < 1e-6)
            return step;
    }
    return 1;
}

base::Region roundedRegion(const base::Rect &rect, double radius)
{
    base::Region region;
    const int height = rect.height();
    radius = std::min(radius, std::min(rect.width(), rect.height()) / 2.0);
    for (int y = 0; y < height; ++y) {
        const double centre = y + 0.5;
        double inset = 0;
        const double dy = centre < radius ? radius - centre
                : centre > height - radius ? centre - (height - radius)
                                           : 0;
        if (dy > 0)
            inset = radius - std::sqrt(std::max(0.0, radius * radius - dy * dy));
        const int left = int(std::lround(inset));
        if (rect.width() - 2 * left > 0)
            region |= base::Rect(rect.x() + left, rect.y() + y, rect.width() - 2 * left, 1);
    }
    return region;
}

} // namespace

class DockWindow::MagnificationFrames final : public ui::FrameClient
{
public:
    explicit MagnificationFrames(DockWindow *dock)
        : FrameClient(dock->m_window->window())
        , m_dock(dock)
    {
    }

    void start()
    {
        if (!isTicking())
            startTicking();
    }

protected:
    void tick(uint64_t frameTimeNs) override
    {
        if (!m_dock->stepMagnification(double(frameTimeNs) / 1e6))
            stopTicking();
    }

private:
    DockWindow *m_dock;
};

class DockWindow::Panel final : public ui::View
{
public:
    explicit Panel(DockWindow *dock)
        : m_dock(dock)
    {
        setTracksHover(true);
    }

protected:
    void hoverChanged() override
    {
        if (!isHovered()) {
            m_dock->hideTooltip();
            m_dock->pointerLeft();
        }
    }

    void hoverMove(const ui::PointerEvent &event) override { m_dock->pointerMoved(event); }

    void layout() override
    {
        if (m_dock->m_glassDark != ui::Theme::current().darkMode
            || m_dock->m_glassOpaque != render::opaqueBackdrop().has_value())
            m_dock->updateGlass();
    }

    void paint(ui::Painter &painter) override
    {
        const ui::Color line = ui::color(toolkit::CompositorColor::DockDivider);
        const base::Rect panel = m_dock->panelRect();
        const auto draw = [&](const base::RectF &rect, double opacity) {
            if (rect.isEmpty() || opacity <= 0)
                return;
            ui::Color color = line;
            color.a = uint8_t(std::lround(255 * opacity));
            painter.drawRoundedRect({panel.x() + rect.x(), panel.y() + rect.y(), rect.width(),
                                     rect.height()},
                                    {0, 0, ui::Corners::Circular}, color);
        };
        draw(m_dock->divider(), 0.8);
        draw(m_dock->trailingDivider(), m_dock->m_trailingOpacity);
    }

    bool canDrop(const ui::DropOffer &) const override { return true; }

    void dragEnter(const ui::DragEvent &event) override
    {
        m_dock->dropEnter(event.offer, event.position);
    }

    uint32_t dragUpdate(const ui::DragEvent &event) override
    {
        return m_dock->dropMove(event.position);
    }

    void dragLeave() override { m_dock->dropLeave(); }

    uint32_t drop(const ui::DragEvent &event) override { return m_dock->dropAt(event.position); }

private:
    DockWindow *m_dock;
};

void DockWindow::callLaunchpad(base::dbus::Connection *bus, const char *method)
{
    if (!bus)
        return;
    bus->send(bus->newMethodCall("org.cutefish.Launchpad", "/Launchpad", "org.cutefish.Launchpad",
                                 method));
}

DockWindow::DockWindow(Shell *shell, std::unique_ptr<Dock> dock)
    : m_shell(shell)
    , m_dock(std::move(dock))
    , m_raiseTimer(core::mainLoop())
    , m_reorderTimer(core::mainLoop())
    , m_tooltipHide(core::mainLoop())
    , m_dockMenuWait(core::mainLoop())
    , m_strutSettle(core::mainLoop())
{
    m_window = std::make_unique<ShellWindow>(shell, wm::WindowRole::Dock);
    m_window->setFocusPolicy(wm::FocusPolicy::NoFocus);
    auto panel = std::make_unique<Panel>(this);
    m_panel = panel.get();
    m_window->setContentView(std::move(panel));
    m_window->onFrameProduced = [this] {
        handOffLanded();
        if (std::exchange(m_releaseReturn, false))
            m_shell->seat()->drag()->releaseReturn();
    };

    ui::Window *window = m_window->window();
    m_applications = std::make_unique<DockPresentation>(window);
    m_minimized = std::make_unique<DockPresentation>(window);
    m_applications->setOpensForNewIcons(true);
    m_applications->onChanged = [this] { layoutItems(); };
    m_minimized->onChanged = [this] { layoutItems(); };
    m_magnificationFrames = std::make_unique<MagnificationFrames>(this);

    m_tooltip = std::make_unique<DockTooltip>(this, shell);
    m_trash = m_panel->addChild<DockIcon>(this, DockIcon::Kind::Trash);
    m_trash->setToolTip(tr("Trash"));

    m_tooltipHide.setSingleShot(true);
    m_tooltipHide.setInterval(std::chrono::milliseconds(150));
    m_tooltipHide.onTimeout = [this] { hideTooltip(); };
    m_dockMenuWait.setSingleShot(true);
    m_dockMenuWait.setInterval(std::chrono::milliseconds(150));
    m_strutSettle.setSingleShot(true);
    m_strutSettle.setInterval(std::chrono::milliseconds(200));
    m_strutSettle.onTimeout = [this] { updateStrut(); };
    m_raiseTimer.setSingleShot(true);
    m_raiseTimer.setInterval(std::chrono::milliseconds(300));
    m_raiseTimer.onTimeout = [this] {
        if (m_drop.hovered && m_drop.hovered->kind() == DockIcon::Kind::Application)
            m_dock->applications().raiseWindow(m_drop.hovered->key());
    };
    m_reorderTimer.setSingleShot(true);
    m_reorderTimer.setInterval(std::chrono::milliseconds(90));
    m_reorderTimer.onTimeout = [this] {
        // Wait out the previous swap's animation, or the sliding thumbnails
        // each claim the drag and it bounces.
        if (m_minimized->isRunning()) {
            m_reorderTimer.start();
            return;
        }
        reorderMinimized();
    };

    input::DragController *drag = m_shell->seat()->drag();
    m_subscriptions.push_back(drag->started.connect([this] { onDragStarted(); }));
    m_subscriptions.push_back(drag->locationChanged.connect([this](const base::PointF &location) {
        onDragMoved(location);
    }));
    m_subscriptions.push_back(drag->released.connect([this] {
        if (m_drag && m_drag->started)
            m_drag->removeOnDrop = m_drag->removes;
    }));
    m_subscriptions.push_back(drag->ended.connect([this] { onDragEnded(); }));
    m_subscriptions.push_back(drag->returned.connect([this] {
        if (!m_drag || !m_drag->returning)
            return;
        finishIconDrag();
        m_releaseReturn = true;
        m_panel->requestPaint();
    }));

    DockModel &applications = m_dock->applications();
    base::dbus::Connection *bus = m_shell->bus();
    applications.hideLaunchpad = [bus] { callLaunchpad(bus, "hide"); };
    applications.toggleLaunchpad = [bus] { callLaunchpad(bus, "toggle"); };
    applications.launched = [this](const std::string &id) {
        if (DockIcon *icon = applicationIcon(id))
            icon->bounce();
    };
    m_dock->minimizedWindows().hideLaunchpad = [bus] { callLaunchpad(bus, "hide"); };
    m_dock->primaryGeometry = [this] { return m_announcedGeometry; };

    const auto changed = [this] {
        rebuild();
        resize(true);
    };
    for (ListModelSignals *model : std::initializer_list<ListModelSignals *> {
                 &applications, &m_dock->minimizedWindows()}) {
        m_subscriptions.push_back(model->rowsInserted.connect([changed](int, int) { changed(); }));
        m_subscriptions.push_back(model->rowsRemoved.connect([changed](int, int) { changed(); }));
        m_subscriptions.push_back(model->rowMoved.connect([changed](int, int) { changed(); }));
        m_subscriptions.push_back(model->dataChanged.connect([this](int, int) { rebuild(); }));
        m_subscriptions.push_back(model->modelReset.connect(changed));
    }
    m_subscriptions.push_back(applications.sectionsChanged.connect(changed));
    m_subscriptions.push_back(m_shell->badges().changed.connect([this] {
        for (const auto &[id, icon] : m_applicationIcons)
            icon->setCount(m_shell->badges().count(id));
    }));
    m_subscriptions.push_back(applications.loaded.connect([this] {
        rebuild();
        resize(false);
        m_applications->setAnimationsEnabled(true);
        m_minimized->setAnimationsEnabled(true);
    }));
    DockSettings &settings = m_dock->settings();
    m_subscriptions.push_back(settings.ready.connect([this] { resize(false); }));
    m_subscriptions.push_back(settings.iconSizeChanged.connect([this] {
        resize(false);
        m_strutSettle.start();
    }));
    m_subscriptions.push_back(settings.directionChanged.connect([this] {
        hideTooltip();
        resize(false);
    }));
    m_subscriptions.push_back(settings.visibilityChanged.connect([this] { resize(false); }));
    m_subscriptions.push_back(settings.styleChanged.connect([this] {
        hideTooltip();
        resize(false);
        updateGlass();
    }));
    m_subscriptions.push_back(settings.glassTintChanged.connect([this] { updateGlass(); }));
    m_subscriptions.push_back(settings.minimizedAreaChanged.connect([this] {
        m_sentTargets.clear();
        updateMinimizeTargets();
    }));
    m_subscriptions.push_back(settings.magnifiedIconSizeChanged.connect([this] {
        resize(false);
        resumeMagnification(false);
    }));
    m_subscriptions.push_back(m_dock->trash().emptyChanged.connect([this] { rebuild(); }));
    m_subscriptions.push_back(ui::Application::instance()->languageChanged.connect([this] { retranslate(); }));
    m_subscriptions.push_back(m_shell->outputsChanged.connect([this] { resize(false); }));
    m_subscriptions.push_back(m_shell->windowManager()->exclusiveZonesChanged.connect([this] {
        if (windowRect(windowThickness()) != m_windowRect)
            resize(false);
    }));

    m_shell->windowManager()->setApplicationQuit([this](wm::Window *window) {
        return m_dock->running().quit(m_shell->tasks().applicationOf(window->id()));
    });

    TaskWindows &windows = m_shell->taskWindows();
    m_subscriptions.push_back(windows.windowAdded.connect([this](uint64_t) {
        updateMinimizeTargets();
    }));
    m_subscriptions.push_back(windows.windowChanged.connect([this](uint64_t, uint32_t changes) {
        if (changes & (kGeometryChange | kStateChange | kSkipTaskbarChange))
            updateMinimizeTargets();
    }));
    m_subscriptions.push_back(windows.windowRemoved.connect([this](uint64_t id) {
        m_sentTargets.erase(id);
    }));
    m_subscriptions.push_back(windows.minimizeLanded.connect([this](uint64_t id) {
        // The model reveals the thumbnail; the window manager stops drawing
        // the window once a frame with it is composed.
        if (std::find(m_landed.begin(), m_landed.end(), id) == m_landed.end())
            m_landed.push_back(id);
        m_window->window()->contentView()->requestPaint();
    }));

    rebuild();
    resize(false);
    if (applications.isLoaded()) {
        m_applications->setAnimationsEnabled(true);
        m_minimized->setAnimationsEnabled(true);
    }
    m_window->show();
}

DockWindow::~DockWindow()
{
    *m_alive = false;
    m_shell->windowManager()->setApplicationQuit({});
    m_subscriptions.clear();
    m_retired.clear();
    m_dock->primaryGeometry = nullptr;
    m_menu.reset();
    m_tooltip.reset();
    m_magnificationFrames.reset();
    m_applications.reset();
    m_minimized.reset();
    m_window.reset();
    m_dock.reset();
}

double DockWindow::pixelRatio() const
{
    return m_window->scale();
}

base::Rect DockWindow::primaryGeometry() const
{
    return m_restPanelRect.translated(m_windowRect.topLeft());
}

double DockWindow::cornerRadius() const
{
    const DockSettings &settings = m_dock->settings();
    const bool bottom = settings.direction() == DockSettings::Bottom;
    const double thickness = bottom ? m_restPanelRect.height() : m_restPanelRect.width();
    const double baseRadius = settings.style() == DockSettings::Docked ? 16.0 : 28.0;
    return (thickness > 0 ? thickness : m_metrics.thickness()) * baseRadius / 86.0;
}

base::Rect DockWindow::windowRect(int thickness) const
{
    const Shell::Output *output = m_shell->output({});
    if (!output)
        return {};
    const base::Rect screen = output->bounds;
    const base::Rect available = m_shell->workArea(*output);
    const DockSettings &settings = m_dock->settings();
    const bool bottom = settings.direction() == DockSettings::Bottom;
    // Whole device steps keep the panel's device pixels whatever the window's
    // size.
    const int step = deviceStep(output->scale);
    const int margins = settings.magnification() ? 0 : edgeGap() / step * step;
    const int length = (bottom ? screen.width() : available.height()) - 2 * margins;
    const int across = windowAcross(thickness);
    switch (settings.direction()) {
    case DockSettings::Left:
        return base::Rect(screen.x(), available.y() + (available.height() - length) / 2, across,
                          length);
    case DockSettings::Right:
        return base::Rect(screen.x() + screen.width() - across,
                          available.y() + (available.height() - length) / 2, across, length);
    case DockSettings::Bottom:
        break;
    }
    return base::Rect(screen.x() + (screen.width() - length) / 2,
                      screen.y() + screen.height() - across, length, across);
}

int DockWindow::windowThickness() const
{
    const DockSettings &settings = m_dock->settings();
    if (!settings.magnification())
        return restWindowThickness();
    const DockMetrics metrics = DockMetrics::forIconSize(windowIconSize());
    const int magnified = metrics.dotAreaSize()
            + std::max(metrics.iconSize, DockSettings::kLargestMagnifiedIconSize)
            + std::max(metrics.outerPadding, metrics.bounceHeight());
    return std::max(restWindowThickness(), magnified);
}

int DockWindow::restWindowThickness() const
{
    const DockMetrics metrics = DockMetrics::forIconSize(windowIconSize());
    return metrics.thickness() + metrics.bounceRoom();
}

int DockWindow::windowIconSize() const
{
    return std::max(m_metrics.iconSize, int(std::ceil(m_fromMetrics.iconSize)));
}

int DockWindow::magnifiedIconSize() const
{
    return std::max(m_metrics.iconSize, m_dock->settings().magnifiedIconSize());
}

int DockWindow::magnifiedReach() const
{
    return m_dock->settings().magnification() ? magnifiedIconSize() - m_metrics.iconSize : 0;
}

int DockWindow::windowAcross(int thickness) const
{
    const Shell::Output *output = m_shell->output({});
    const int step = output ? deviceStep(output->scale) : 1;
    return (thickness + edgeGap() + step - 1) / step * step;
}

int DockWindow::edgeGap() const
{
    return m_dock->settings().style() == DockSettings::Docked ? 0 : m_dock->settings().edgeMargins() / 2;
}

render::GlassParams DockWindow::glass() const
{
    const bool dark = ui::Theme::current().darkMode;
    // Rounded to 16 bits a component, then wl_fixed.
    const auto component = [](double v) {
        return glassFixed(std::round(std::clamp(v, 0.0, 1.0) * 65535.0) / 65535.0);
    };
    if (render::opaqueBackdrop()) {
        const ui::Color background = ui::Theme::current().background;
        const double shade = dark ? 1.0 : kOpaqueLightShade;
        render::GlassParams glass = fishuiGlass();
        glass.ignoresOpaqueBackdrop = true;
        glass.blurSigma = glassFixed(kOpaqueBlurSigma);
        glass.displacement = 0;
        glass.chroma = 0;
        glass.saturation = 1;
        glass.rim = glassFixed(1.0);
        glass.tintRed = component(background.r / 255.0 / shade);
        glass.tintGreen = component(background.g / 255.0 / shade);
        glass.tintBlue = component(background.b / 255.0 / shade);
        glass.tintAlpha = component(kOpaqueTintAlpha);
        return glass;
    }
    const double amount = glassAmount(m_dock->settings().glassTint());
    const auto blend = [amount](double clear, double middle, double tinted) {
        return glassBlend(amount, clear, middle, tinted);
    };
    render::GlassParams glass = fishuiGlass();
    // The glass blur kernel is 30 device pixels wide; a larger sigma truncates.
    glass.blurSigma = glassFixed(blend(0.6, 1.0, 8.0));
    glass.displacement = glassFixed(blend(36.0, 36.0, 0.0));
    glass.chroma = 0;
    glass.saturation = glassFixed(blend(1.4, 1.3, 1.0));
    glass.rim = glassFixed(1.0);
    const double alpha = dark ? blend(0.12, 0.29, 0.50) : blend(0.16, 0.25, 0.53);
    const double value = (dark ? blend(0.046, 0.110, 0.190) : blend(0.060, 0.150, 0.530)) / alpha;
    glass.tintRed = glass.tintGreen = glass.tintBlue = component(value);
    glass.tintAlpha = component(alpha);
    return glass;
}

std::vector<DockIcon *> DockWindow::icons() const
{
    std::vector<std::pair<double, DockIcon *>> ordered;
    for (const auto &[key, icon] : m_applicationIcons)
        ordered.push_back({icon->geometry().x + icon->geometry().y, icon});
    std::sort(ordered.begin(), ordered.end());
    std::vector<DockIcon *> result;
    for (const auto &[position, icon] : ordered)
        result.push_back(icon);
    std::vector<std::pair<double, DockIcon *>> minimized;
    for (const auto &[id, icon] : m_minimizedIcons)
        minimized.push_back({icon->geometry().x + icon->geometry().y, icon});
    std::sort(minimized.begin(), minimized.end());
    for (const auto &[position, icon] : minimized)
        result.push_back(icon);
    result.push_back(m_trash);
    return result;
}

DockIcon *DockWindow::applicationIcon(const std::string &id) const
{
    const auto it = m_applicationIcons.find(id);
    return it != m_applicationIcons.end() ? it->second : nullptr;
}

DockIcon *DockWindow::minimizedIcon(uint64_t window) const
{
    const auto it = m_minimizedIcons.find(window);
    return it != m_minimizedIcons.end() ? it->second : nullptr;
}

base::Region DockWindow::inputBand(bool tracking) const
{
    base::Rect rect = m_restPanelRect;
    const int reach = tracking ? magnifiedReach() : 0;
    const DockSettings::Direction direction = m_dock->settings().direction();
    const base::Rect gap = direction == DockSettings::Bottom
            ? base::Rect::fromEdges(rect.x(), rect.y() + rect.height() / 2, rect.right(),
                                    m_windowRect.height())
            : direction == DockSettings::Left
            ? base::Rect::fromEdges(0, rect.y(), rect.x() + rect.width() / 2, rect.bottom())
            : base::Rect::fromEdges(rect.x() + rect.width() / 2, rect.y(), m_windowRect.width(),
                                    rect.bottom());
    if (direction == DockSettings::Bottom)
        rect.setTop(rect.top() - reach);
    else if (direction == DockSettings::Left)
        rect.setRight(rect.right() + reach);
    else
        rect.setLeft(rect.left() - reach);
    return roundedRegion(rect, cornerRadius()) | gap;
}

void DockWindow::updateInputRegion()
{
    if (m_restPanelRect.isEmpty())
        return;
    const base::Region region = inputBand(m_magnifier.isTracking() || m_magnifier.isHeld());
    if (region == m_inputRegion)
        return;
    m_inputRegion = region;
    m_window->setInputRegion(region);
}

DockMagnification DockWindow::magnification(double slotsStart, double slotsEnd) const
{
    if (m_magnifier.amount() <= 0 || m_drawn.iconSize <= 0 || slotsEnd <= slotsStart)
        return {};
    const double size = m_drawn.iconSize;
    const double peak = std::max(size, double(m_dock->settings().magnifiedIconSize())) / size;
    const double icon = m_metrics.iconSize;
    const double gap = m_drawn.iconGap;
    const double slotPeak = (peak * icon + gap) / (icon + gap);
    const double focus = std::clamp(m_magnifier.focus(), slotsStart, slotsEnd);
    return DockMagnification(focus, m_magnifier.amount(), slotPeak, icon + gap);
}

void DockWindow::pointerMoved(const ui::PointerEvent &event)
{
    if (event.device == ui::PointerDevice::TouchScreen) {
        pointerLeft();
        return;
    }
    const bool bottom = m_dock->settings().direction() == DockSettings::Bottom;
    if (m_magnifier.track(bottom ? event.position.x : event.position.y))
        magnificationMoved();
}

void DockWindow::pointerLeft()
{
    if (m_magnifier.leave())
        magnificationMoved();
}

void DockWindow::resumeMagnification(bool tracking)
{
    if (!m_magnifier.isEnabled())
        return;
    // A grab may have kept the dock from hearing the pointer leave or come
    // back.
    input::Seat *seat = m_shell->seat();
    const base::PointF location = seat->pointerLocation() - base::PointF(m_windowRect.topLeft());
    const base::Point point(int(std::floor(location.x())), int(std::floor(location.y())));
    const bool bottom = m_dock->settings().direction() == DockSettings::Bottom;
    if (!seat->cursorHiddenByTouch() && inputBand(tracking).contains(point))
        m_magnifier.track(bottom ? location.x() : location.y());
    else
        m_magnifier.leave();
    magnificationMoved();
}

void DockWindow::magnificationMoved()
{
    updateInputRegion();
    m_magnificationFrames->start();
}

bool DockWindow::stepMagnification(double nowMs)
{
    m_magnifier.advance(nowMs);
    layoutItems();
    if (m_magnifier.isMoving())
        return true;
    m_magnifier.stop();
    return false;
}

void DockWindow::prepareMagnification()
{
    const int size = m_dock->settings().magnification() ? magnifiedIconSize() : 0;
    for (const auto &[key, icon] : m_applicationIcons)
        icon->prepareMagnifiedSize(size);
    for (const auto &[id, icon] : m_minimizedIcons)
        icon->prepareMagnifiedSize(size);
    m_trash->prepareMagnifiedSize(size);
}

void DockWindow::updateGlass()
{
    if (m_drawnPanelRect.isEmpty())
        return;
    const double radius = std::min(cornerRadius(),
                                   double(std::min(m_drawnPanelRect.width(), m_drawnPanelRect.height())) / 2);
    const render::GlassParams params = glass();
    base::Rect blur = m_drawnPanelRect;
    if (m_dock->settings().style() == DockSettings::Docked) {
        // Extend past the screen edge so the glass keeps square corners and no
        // rim or bevel along that edge; the surface clips the excess.
        const int extend = int(std::ceil(std::max(radius, double(params.bevel))));
        switch (m_dock->settings().direction()) {
        case DockSettings::Left:
            blur.setLeft(blur.left() - extend);
            break;
        case DockSettings::Right:
            blur.setRight(blur.right() + extend);
            break;
        case DockSettings::Bottom:
            blur.setBottom(blur.bottom() + extend);
            break;
        }
    }
    m_glassDark = ui::Theme::current().darkMode;
    m_glassOpaque = render::opaqueBackdrop().has_value();
    m_window->setBackgroundBlur(blur, int(std::lround(radius)), params);
}

void DockWindow::updateStrut()
{
    const DockSettings &settings = m_dock->settings();
    const Shell::Output *output = m_shell->output({});
    if (!output)
        return;
    const int thickness = m_metrics.thickness() + edgeGap() + 1;
    wm::ScreenEdge edge = wm::ScreenEdge::Bottom;
    switch (settings.direction()) {
    case DockSettings::Left:
        edge = wm::ScreenEdge::Left;
        break;
    case DockSettings::Right:
        edge = wm::ScreenEdge::Right;
        break;
    case DockSettings::Bottom:
        break;
    }
    switch (settings.visibility()) {
    case DockSettings::AlwaysShow:
        m_window->setExclusiveZone(edge, thickness);
        break;
    case DockSettings::IntellHide:
        m_window->setExclusiveZone(edge, thickness, true);
        break;
    case DockSettings::AlwaysHide:
        m_window->setExclusiveZone(edge, 0);
        break;
    }
}

void DockWindow::retranslate()
{
    if (m_menu)
        m_menu->close();
    hideTooltip();
    m_trash->setToolTip(tr("Trash"));
    m_dock->applications().retranslate();
}


} // namespace cutefish::shell
