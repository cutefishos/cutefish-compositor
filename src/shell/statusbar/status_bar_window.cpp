// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/statusbar/status_bar_window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "base/dbus.h"
#include "base/locale.h"
#include "core/main_loop.h"
#include "shell/shell_window.h"
#include "shell/statusbar/clock_format.h"
#include "shell/statusbar/status_bar.h"
#include "shell/statusbar/device_usage.h"
#include "shell/statusbar/status_bar_backdrop.h"
#include "shell/statusbar/status_icon.h"
#include "shell/statusbar/status_items_view.h"
#include "shell/statusbar/system_tray_view.h"
#include "shell/statusbar/vpn_notifier.h"
#include "ui/embedded_window.h"
#include "ui/cell_layout.h"
#include "ui/path.h"
#include "ui/theme.h"
#include "wm/status_items.h"
#include "wm/window.h"
#include "wm/window_manager.h"

namespace cutefish::shell {

using Panel = StatusBarPanels::Panel;

// The bar's row; a press between its items closes every popup.
class StatusBarWindow::Root final : public ui::RowLayout
{
public:
    explicit Root(StatusBarWindow *bar)
        : m_bar(bar)
    {
        setName("statusbar");
        setSpacing(0);
    }

    bool acceptsPointer() const override { return true; }

protected:
    void pointerPress(const ui::PointerEvent &) override { m_bar->closePopups(); }

    void layout() override
    {
        ui::RowLayout::layout();
        // A theme change lays every window out again; the system's dark
        // mode is the bar's fallback.
        if (ui::Theme::generation() != m_themeGeneration) {
            m_themeGeneration = ui::Theme::generation();
            core::mainLoop()->post([bar = m_bar, alive = std::weak_ptr<bool>(m_bar->m_alive)] {
                if (alive.lock())
                    bar->updateLook();
            });
        }
    }

    // The plate moves with its item, wherever in the row it sits, and so do
    // the applications' items.
    void childrenLaidOut() override
    {
        if (m_bar->m_statusItems)
            m_bar->m_statusItems->placeItems();
        m_bar->updatePlateBlur();
    }

private:
    StatusBarWindow *m_bar;
    uint64_t m_themeGeneration = 0;
};

// The menu bar's slot: what it holds fills it.
class MenuSlot final : public ui::View
{
protected:
    void layout() override
    {
        for (const auto &child : children())
            child->setGeometry(localRect());
    }
};

StatusBarWindow::StatusBarWindow(Shell *shell, std::unique_ptr<StatusBar> statusBar)
    : m_shell(shell)
    , m_statusBar(std::move(statusBar))
    , m_backdrop(std::make_unique<StatusBarBackdrop>())
{
    base::dbus::Connection *bus = m_shell->bus();
    m_vpnNotifier = std::make_unique<VpnNotifier>(bus);
    m_vpnNotifier->openSettings = [shell] { shell->openSettings({"-m", "vpn"}); };
    m_window = std::make_unique<ShellWindow>(shell, wm::WindowRole::Panel);
    m_window->setFocusPolicy(wm::FocusPolicy::NoFocus);
    m_window->window()->setBackgroundColor(ui::Color {0, 0, 0, 0});
    auto root = std::make_unique<Root>(this);
    m_root = root.get();

    // Empty until the menu bar's component plugs in.
    m_menuSlot = m_root->addChild<MenuSlot>();
    m_menuSlot->setName("menubar");
    ui::LayoutHints fill;
    fill.fillWidth = true;
    fill.fillHeight = true;
    m_menuSlot->setLayoutHints(fill);
    m_menuSlot->setClipsChildren(true);

    m_recording = m_root->addChild<StopItem>(this);
    m_recording->setName("recording");
    m_recording->setVisible(false);
    m_recording->onClicked = [this](const ui::PointerEvent &) {
        closePopups();
        if (onStopRecording)
            onStopRecording();
    };
    m_agentControl = m_root->addChild<StopItem>(this);
    m_agentControl->setName("agent-control");
    m_agentControl->setVisible(false);
    m_agentControl->onClicked = [this](const ui::PointerEvent &) {
        closePopups();
        if (onStopAgentControl)
            onStopAgentControl();
    };

    m_statusItems = m_root->addChild<StatusItemsView>(
            this, shell->windowManager() ? shell->windowManager()->statusItems() : nullptr);
    m_statusItems->setName("status-items");
    m_statusItems->onWidthChanged = [this] { layoutRow(); };

    SystemTrayView::Host trayHost;
    trayHost.closePanels = [this] { closePanels(); };
    trayHost.closeMenuBar = [this] {
        if (m_menuBar)
            m_menuBar->closeMenu();
    };
    trayHost.closePopups = [this] { closePopups(); };
    trayHost.availableGeometry = [this] { return availableGeometry(); };
    trayHost.origin = [this] {
        const base::Point origin = m_window->geometry().topLeft();
        return ui::PointF {double(origin.x()), double(origin.y())};
    };
    trayHost.shortcutText = [shell](const std::string &key, uint32_t modifiers) {
        return shell->shortcutText(key, modifiers, false);
    };
    m_tray = m_root->addChild<SystemTrayView>(this, &m_statusBar->systemTray(), std::move(trayHost));
    m_tray->setName("tray");
    m_tray->onCountChanged = [this] { layoutRow(); };

    const auto statusIcon = [this](const char *name, Panel panel) {
        StatusIcon *icon = m_root->addChild<StatusIcon>(this);
        icon->setName(name);
        icon->onClicked = [this, panel](const ui::PointerEvent &) { togglePanel(panel); };
        icon->onPreferredWidthChanged = [this] { layoutRow(); };
        return icon;
    };
    m_volume = statusIcon("volume", Panel::Volume);
    m_network = statusIcon("network", Panel::Network);
    m_battery = statusIcon("battery", Panel::Battery);
    m_battery->setIconWidth(BarContext::kIconSize + 6);
    m_control = statusIcon("control", Panel::ControlCenter);
    m_control->setIconName("control");
    m_clock = m_root->addChild<ClockItem>(this);
    m_clock->onClicked = [this](const ui::PointerEvent &) { togglePanel(Panel::NotificationCenter); };
    m_clock->onPreferredWidthChanged = [this] { layoutRow(); };

    m_window->setContentView(std::move(root));

    // The clock, in the primary language's words.
    m_clockFd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
    if (m_clockFd >= 0) {
        m_clockWatch = core::mainLoop()->watch(m_clockFd, EPOLLIN, [this](uint32_t) {
            uint64_t expirations = 0;
            // Fails with ECANCELED when the system time was set.
            [[maybe_unused]] const ssize_t size = read(m_clockFd, &expirations, sizeof(expirations));
            updateClock();
        });
    }
    m_subscriptions.push_back(m_statusBar->timeFormatChanged.connect([this] { updateClock(); }));

    Battery &battery = m_statusBar->battery();
    m_subscriptions.push_back(battery.changed.connect([this] { updateBattery(); }));
    m_subscriptions.push_back(battery.showPercentageChanged.connect([this] { updateBattery(); }));

    m_subscriptions.push_back(m_backdrop->changed.connect([this] { updateLook(); }));
    if (bus)
        setDeviceUsage(DeviceUsage::create(&m_shell->tasks()));
    m_subscriptions.push_back(m_shell->outputsChanged.connect([this] { updateGeometry(); }));
    // The theme's dark mode stands in while nothing behind the bar was judged.
    m_subscriptions.push_back(m_window->mappedChanged.connect([this] { updateLook(); }));

    updateGeometry();
    updateBattery();
    updateIndicators();
    updateLook();
    updateClock();
    m_window->show();
    if (wm::StatusItems *items = m_shell->windowManager() ? m_shell->windowManager()->statusItems() : nullptr) {
        items->setBar(m_window->surface(), m_window->shellSurface());
        m_subscriptions.push_back(items->committed.connect([this] { m_window->requestFrame(); }));
        updateStatusItemLimits();
        updateLook();
    }
}

StatusBarWindow::~StatusBarWindow()
{
    m_subscriptions.clear();
    m_panelSubscriptions.clear();
    m_indicatorsChanged.reset();
    m_deviceUsageChanged.reset();
    m_backdrop->follow(nullptr);
    if (wm::StatusItems *items = m_shell->windowManager() ? m_shell->windowManager()->statusItems() : nullptr)
        items->setBar(nullptr, nullptr);
    m_window.reset();
    if (m_clockWatch)
        core::mainLoop()->unwatch(m_clockWatch);
    if (m_clockFd >= 0)
        close(m_clockFd);
}

void StatusBarWindow::updateGeometry()
{
    // Always the primary output, all its width.
    const Shell::Output *output = m_shell->output({});
    if (!output)
        return;
    m_window->setOutput(output->name);
    m_window->setGeometry(base::Rect(output->bounds.x(), output->bounds.y(), output->bounds.width(), kHeight));
    m_window->setExclusiveZone(wm::ScreenEdge::Top, kHeight);
    updateStatusItemLimits();
}

void StatusBarWindow::updateStatusItemLimits()
{
    wm::StatusItems *items = m_shell->windowManager() ? m_shell->windowManager()->statusItems() : nullptr;
    if (!items || !m_window)
        return;
    // An item may take up to a third of the bar.
    const base::Rect geometry = m_window->geometry();
    items->setBarOrigin(geometry.topLeft());
    items->setLimits(kHeight, std::max(kHeight, geometry.width() / 3));
}

void StatusBarWindow::setWallpaper(Wallpaper *wallpaper)
{
    m_backdrop->follow(wallpaper);
}

bool StatusBarWindow::lightContents() const
{
    return m_lightContents;
}

bool StatusBarWindow::panelOpen() const
{
    for (StatusBarPanels *panels : m_panels) {
        for (Panel panel : {Panel::ControlCenter, Panel::Network, Panel::Volume, Panel::Battery,
                            Panel::NotificationCenter}) {
            if (panels->hasPanel(panel) && panels->isOpen(panel))
                return true;
        }
    }
    return false;
}

void StatusBarWindow::updateLook()
{
    // What is behind the bar decides; the system's dark mode while nothing
    // has been judged.
    const bool light = m_backdrop->isValid() ? m_backdrop->useLightForeground() : ui::Theme::current().darkMode;
    m_lightContents = light;
    if (m_menuBar)
        m_menuBar->barChanged();
    m_tray->barChanged();
    m_statusItems->barChanged();
    if (wm::StatusItems *items = m_shell->windowManager() ? m_shell->windowManager()->statusItems() : nullptr) {
        items->setAppearance(light, textColor().argb());
    }
    for (StatusIcon *icon : {m_volume, m_network, m_battery, m_control})
        icon->updateLook();
    m_clock->updateLook();
    m_recording->updateLook();
    m_agentControl->updateLook();
}

void StatusBarWindow::platePainted()
{
    updatePlateBlur();
}

static const BarItem *findPlate(const ui::View *view)
{
    if (!view->isVisible())
        return nullptr;
    if (const auto *item = dynamic_cast<const BarItem *>(view); item && item->isPlateShown())
        return item;
    for (const auto &child : view->children()) {
        if (const BarItem *item = findPlate(child.get()))
            return item;
    }
    return nullptr;
}

void StatusBarWindow::updatePlateBlur()
{
    if (!m_window || !m_root)
        return;
    // A surface has one blur, and one plate shows at a time: opening a panel
    // or a menu closes the others.
    const BarItem *item = findPlate(m_root);
    ui::RectF plate = item ? item->plateRect() : ui::RectF();
    if (plate.width > 0 && plate.height > 0) {
        const ui::PointF origin = item->mapToWindow({plate.x, plate.y});
        plate = {origin.x, origin.y, plate.width, plate.height};
    } else {
        plate = {};
    }
    if (plate == m_blurPlate)
        return;
    m_blurPlate = plate;
    if (plate.width <= 0) {
        m_window->setBackgroundBlur(base::Rect(), 0);
        return;
    }
    // The rectangle in whole logical pixels; the shape clips the blur to the
    // plate as drawn, which sits on fractional ones at most scales.
    const double radius = ui::Theme::current().smallRadius;
    const int left = int(std::floor(plate.x));
    const int top = int(std::floor(plate.y));
    const base::Rect rect(left, top, int(std::ceil(plate.x + plate.width)) - left,
                          int(std::ceil(plate.y + plate.height)) - top);
    ui::Path shape;
    shape.addRoundedRect(plate, radius, radius);
    m_window->setBackgroundBlur(rect, int(std::lround(radius)), {}, shape);
}

void StatusBarWindow::updateBattery()
{
    const Battery &battery = m_statusBar->battery();
    m_battery->showBattery(battery, battery.showPercentage());
    layoutRow();
}

void StatusBarWindow::setIndicators(StatusBarIndicators *indicators)
{
    m_indicatorsChanged.reset();
    m_indicators = indicators;
    if (m_indicators)
        m_indicatorsChanged = m_indicators->changed.connect([this] { updateIndicators(); });
    updateIndicators();
}

void StatusBarWindow::updateIndicators()
{
    const StatusBarIndicators::Audio audio = m_indicators ? m_indicators->audio() : StatusBarIndicators::Audio();
    m_volume->setVisible(audio.hasSink);
    const double level = audio.hasSink ? audio.volume * 100.0 : -1;
    const char *volumeIcon = level <= 0 ? "audio-volume-muted-symbolic"
            : level <= 25              ? "audio-volume-low-symbolic"
            : level <= 75              ? "audio-volume-medium-symbolic"
                                       : "audio-volume-high-symbolic";
    m_volume->setIconName(audio.hasSink && !audio.muted ? volumeIcon : "audio-volume-muted-symbolic");

    m_network->showNetwork(m_indicators ? m_indicators->network() : StatusBarIndicators::Network());

    layoutRow();
}

void StatusBarWindow::setDeviceUsage(std::unique_ptr<DeviceUsage> usage)
{
    m_deviceUsageChanged.reset();
    m_deviceUsage = std::move(usage);
    const auto update = [this] {
        // Camera before microphone before system audio.
        const DeviceUsage *usage = m_deviceUsage.get();
        m_control->setIndicatorColor(usage && usage->camera()            ? std::optional<ui::Color>(ui::accent::green)
                                     : usage && usage->microphone()    ? std::optional<ui::Color>(ui::accent::orange)
                                     : usage && usage->systemAudio()   ? std::optional<ui::Color>(ui::accent::purple)
                                                                       : std::nullopt);
        layoutRow();
    };
    if (m_deviceUsage)
        m_deviceUsageChanged = m_deviceUsage->usesChanged.connect(update);
    update();
}

void StatusBarWindow::setRecording(bool recording)
{
    m_recording->setVisible(recording);
    layoutRow();
}

void StatusBarWindow::setAgentControl(bool active)
{
    m_agentControl->setVisible(active);
    layoutRow();
}

void StatusBarWindow::layoutRow()
{
    // Not before every item exists.
    if (!m_clock)
        return;
    const auto preferred = [](ui::View *view, double width) {
        ui::LayoutHints hints;
        hints.preferredWidth = width;
        hints.fillHeight = true;
        if (view->layoutHints().preferredWidth != width || !view->layoutHints().fillHeight)
            view->setLayoutHints(hints);
    };
    preferred(m_recording, m_recording->preferredWidth());
    preferred(m_agentControl, m_agentControl->preferredWidth());
    m_statusItems->setVisible(m_statusItems->count() > 0);
    preferred(m_statusItems, m_statusItems->preferredWidth());
    m_tray->setVisible(m_tray->count() > 0);
    preferred(m_tray, m_tray->preferredWidth());
    for (StatusIcon *icon : {m_volume, m_network, m_battery, m_control})
        preferred(icon, icon->preferredWidth());
    preferred(m_clock, m_clock->preferredWidth());
    m_root->requestLayout();
}

void StatusBarWindow::updateClock()
{
    const auto now = std::chrono::system_clock::now();
    const int64_t seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const base::Locale locale(m_statusBar->timeLocale());
    m_clock->setText(locale.toString(base::DateTime::fromSecondsSinceEpoch(seconds), m_statusBar->timeFormat()));
    armClock();
}

void StatusBarWindow::armClock()
{
    if (m_clockFd < 0)
        return;
    timespec now {};
    clock_gettime(CLOCK_REALTIME, &now);
    const time_t period = clock_format::showsSeconds(m_statusBar->timeFormat()) ? 1 : 60;
    itimerspec spec {};
    spec.it_value.tv_sec = (now.tv_sec / period + 1) * period;
    timerfd_settime(m_clockFd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &spec, nullptr);
}

void StatusBarWindow::addPanels(StatusBarPanels *panels)
{
    if (!panels || std::find(m_panels.begin(), m_panels.end(), panels) != m_panels.end())
        return;
    m_panels.push_back(panels);
    m_panelSubscriptions.push_back(panels->panelsChanged.connect([this] { updateChecked(); }));
    updateChecked();
}

void StatusBarWindow::removePanels(StatusBarPanels *panels)
{
    const auto it = std::find(m_panels.begin(), m_panels.end(), panels);
    if (it == m_panels.end())
        return;
    m_panelSubscriptions.erase(m_panelSubscriptions.begin() + (it - m_panels.begin()));
    m_panels.erase(it);
    updateChecked();
}

StatusBarPanels *StatusBarWindow::panelsFor(Panel panel) const
{
    for (StatusBarPanels *panels : m_panels) {
        if (panels->hasPanel(panel))
            return panels;
    }
    return nullptr;
}

BarItem *StatusBarWindow::itemFor(Panel panel) const
{
    switch (panel) {
    case Panel::ControlCenter:
        return m_control;
    case Panel::Network:
        return m_network;
    case Panel::Volume:
        return m_volume;
    case Panel::Battery:
        return m_battery;
    case Panel::NotificationCenter:
        return m_clock;
    }
    return nullptr;
}

void StatusBarWindow::updateChecked()
{
    for (Panel panel : {Panel::ControlCenter, Panel::Network, Panel::Volume, Panel::Battery,
                        Panel::NotificationCenter}) {
        StatusBarPanels *panels = panelsFor(panel);
        itemFor(panel)->setChecked(panels && panels->isOpen(panel));
    }
}

void StatusBarWindow::togglePanel(Panel panel)
{
    StatusBarPanels *panels = panelsFor(panel);
    if (!panels)
        return;
    const bool wasOpen = panels->isOpen(panel);
    closePopups();
    if (wasOpen)
        return;
    BarItem *item = itemFor(panel);
    const ui::PointF origin = item->mapToWindow({0, 0});
    const base::Rect anchor(int(std::lround(origin.x)), int(std::lround(origin.y)),
                            int(std::lround(item->geometry().width)), int(std::lround(item->geometry().height)));
    panels->openPanel(panel, m_window.get(), anchor);
}

void StatusBarWindow::closePanels()
{
    for (StatusBarPanels *panels : m_panels)
        panels->closePanels();
}

void StatusBarWindow::closePopups()
{
    closePanels();
    if (m_menuBar)
        m_menuBar->closeMenu();
    m_tray->closeMenu();
}

StatusBarMenuBarHost StatusBarWindow::menuBarHost()
{
    StatusBarMenuBarHost host;
    host.bar = this;
    host.statusBar = m_statusBar.get();
    host.closePanels = [this] { closePanels(); };
    host.closeTrayMenu = [this] { m_tray->closeMenu(); };
    host.availableGeometry = [this] { return availableGeometry(); };
    return host;
}

void StatusBarWindow::setMenuBar(std::unique_ptr<StatusBarMenuBar> menuBar)
{
    if (m_menuBar) {
        m_menuBar->closeMenu();
        m_menuBar->setVisible(false);
        std::shared_ptr<ui::View> retired(m_menuSlot->takeChild(m_menuBar));
        core::mainLoop()->post([retired] {});
        m_menuBar = nullptr;
    }
    if (!menuBar)
        return;
    m_menuBar = menuBar.get();
    m_menuSlot->addChild(std::move(menuBar));
    m_menuBar->barChanged();
    m_menuSlot->requestLayout();
}

ui::RectF StatusBarWindow::availableGeometry() const
{
    // The menus stay inside the work area the shell reserves.
    const Shell::Output *output = m_shell->output(m_window->outputName());
    if (!output)
        return {};
    const base::Rect area = m_shell->workArea(*output);
    const base::Point origin = m_window->geometry().topLeft();
    return {double(area.x() - origin.x()), double(area.y() - origin.y()), double(area.width()),
            double(area.height())};
}

} // namespace cutefish::shell
