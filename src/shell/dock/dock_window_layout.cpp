// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/dock/dock_window.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "core/main_loop.h"
#include "shell/common/application_badges.h"
#include "shell/common/task_windows.h"
#include "shell/dock/dock.h"
#include "shell/dock/dock_icon.h"
#include "shell/dock/dock_presentation.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "wm/window.h"
#include "wm/window_manager.h"

namespace cutefish::shell {

namespace {

base::Rect fitToWindow(const base::Rect &slot, const base::Rect &window)
{
    if (slot.isEmpty() || window.isEmpty())
        return slot;
    const double scale = std::min(double(slot.width()) / window.width(),
                                  double(slot.height()) / window.height());
    const double width = window.width() * scale;
    const double height = window.height() * scale;
    const double x = slot.x() + slot.width() / 2.0 - width / 2;
    const double y = slot.y() + slot.height() / 2.0 - height / 2;
    const int left = int(std::floor(x));
    const int top = int(std::floor(y));
    const base::Rect aligned(left, top, int(std::ceil(x + width)) - left,
                             int(std::ceil(y + height)) - top);
    return aligned.intersected(slot);
}

} // namespace

void DockWindow::rebuild()
{
    DockModel &applications = m_dock->applications();
    std::vector<std::string> keys;
    std::map<std::string, DockIcon *> icons;
    for (int row = 0; row < applications.count(); ++row) {
        const DockItem &item = applications.at(row);
        keys.push_back(item.id);
        DockIcon *icon = applicationIcon(item.id);
        if (!icon) {
            icon = m_panel->addChild<DockIcon>(this, DockIcon::Kind::Application);
            icon->setKey(item.id);
        }
        icon->setDraggable(!item.fixed);
        if (item.id == DockModel::kLaunchpadId)
            icon->setIcon(shellDataFile("launchpad.svg"), true);
        else if (item.missing)
            icon->setIcon(shellDataFile("application-missing.svg"));
        else
            icon->setIcon(item.iconName.empty() ? "application-x-desktop" : item.iconName);
        icon->setToolTip(item.visibleName);
        icon->setRunning(item.running);
        icon->setCount(m_shell->badges().count(item.id));
        icons[item.id] = icon;
    }
    for (const auto &[key, icon] : m_applicationIcons) {
        if (!icons.contains(key))
            icons[key] = icon;
    }
    m_applicationIcons = std::move(icons);
    m_applications->setKeys(keys);

    MinimizedWindows &minimized = m_dock->minimizedWindows();
    std::vector<std::string> windowKeys;
    std::map<uint64_t, DockIcon *> windowIcons;
    for (int row = 0; row < minimized.count(); ++row) {
        const MinimizedWindows::Entry &entry = minimized.entries()[size_t(row)];
        windowKeys.push_back(std::to_string(entry.id));
        DockIcon *icon = minimizedIcon(entry.id);
        if (!icon) {
            icon = m_panel->addChild<DockIcon>(this, DockIcon::Kind::Minimized);
            icon->setKey(std::to_string(entry.id));
            icon->setWindowId(entry.id);
            icon->setDraggable(true);
        }
        if (m_shell->taskWindows().window(entry.id)) {
            const std::string iconName = minimized.iconName(row);
            icon->setIcon(iconName.empty() ? "application-x-desktop" : iconName);
            icon->setToolTip(minimized.title(row));
            icon->setThumbnail(minimized.thumbnail(row), minimized.aspect(row));
            icon->setBadgeOpacity(entry.landed ? 1 : 0);
        }
        windowIcons[entry.id] = icon;
        m_minimizedStates[entry.id] = entry;
    }
    for (const auto &[id, icon] : m_minimizedIcons) {
        if (!windowIcons.contains(id))
            windowIcons[id] = icon;
    }
    m_minimizedIcons = std::move(windowIcons);
    m_minimized->setKeys(windowKeys);

    m_trash->setIcon(m_dock->trash().isEmpty() ? "user-trash-empty" : "user-trash-full");
    prepareMagnification();
    layoutItems();
}

void DockWindow::updateMetrics(bool animate)
{
    const DockSettings &settings = m_dock->settings();
    const DockModel &applications = m_dock->applications();
    const Shell::Output *output = m_shell->output({});
    const bool bottom = settings.direction() == DockSettings::Bottom;
    const int itemCount = applications.count() + (m_drop.gap ? 1 : 0) + 1 + m_dock->minimizedWindows().count();
    const int bound = output ? (bottom ? output->bounds.width() : m_shell->workArea(*output).height()) : 0;
    const int separatorCount = applications.count() > applications.pinnedCount() ? 2 : 1;
    const auto fits = [&](const DockMetrics &metrics) {
        const int room = std::max(edgeGap(), metrics.endRoom());
        return metrics.length(itemCount) + separatorCount * metrics.sectionExtent() <= bound - 2 * room;
    };

    DockMetrics metrics = DockMetrics::forIconSize(settings.iconSize());
    while (output && !fits(metrics) && metrics.iconSize > 1)
        metrics = DockMetrics::forIconSize(metrics.iconSize - 1);

    animate = animate && m_applications->animationsEnabled();
    const bool changed = metrics != m_metrics;
    if (changed) {
        // Taken before the models restart the resize.
        m_fromEnds = m_metrics.iconSize + 2 * m_metrics.lengthPadding;
        m_fromMetrics = animate ? m_drawn : DrawnDockMetrics::of(metrics);
        m_metrics = metrics;
        m_applications->setPitch(metrics.pitch(), animate);
        m_minimized->setPitch(metrics.pitch(), animate);
        m_applications->setItemSize(metrics.iconSize);
        m_minimized->setItemSize(metrics.iconSize);
        if (!animate)
            m_fromEnds = -1;
    }
    m_applications->setSection(applications.pinnedCount(), metrics.sectionExtent(), animate);
}

void DockWindow::resize(bool animate)
{
    if (!m_dock->settings().isReady() && !m_windowRect.isEmpty())
        return;
    m_strutSettle.stop();
    updateMetrics(animate);
    const DockSettings &settings = m_dock->settings();
    m_magnifier.setEnabled(settings.magnification());
    const Shell::Output *output = m_shell->output({});
    if (output)
        m_window->setOutput(output->name);
    m_window->setEdgeReach(settings.magnification() ? windowAcross(restWindowThickness()) : 0);
    m_windowRect = windowRect(windowThickness());
    if (!m_windowRect.isEmpty())
        m_window->setGeometry(m_windowRect);
    m_surfaceLength = settings.direction() == DockSettings::Bottom ? m_windowRect.width() : m_windowRect.height();
    prepareMagnification();
    layoutItems();
    updateInputRegion();
    if (!m_drop.gap)
        updateStrut();
    updateMinimizeTargets();
}

double DockWindow::place(double position, double rest) const
{
    const double ratio = pixelRatio();
    return rest + std::round((position - rest) * ratio) / ratio;
}

void DockWindow::updateLayoutStart()
{
    const double ends = [this] {
        const double target = m_metrics.iconSize + 2 * m_metrics.lengthPadding;
        if (m_fromEnds < 0)
            return target;
        return m_fromEnds + (target - m_fromEnds) * m_applications->resizeProgress();
    }();
    m_minimizedStart = m_applications->length() + m_applications->trailingExtent();
    m_length = m_minimizedStart + m_minimized->length() + ends;
    const double resting = m_applications->targetLength() + m_applications->targetTrailingExtent()
            + m_minimized->targetLength() + m_metrics.iconSize + 2 * m_metrics.lengthPadding;
    const auto restingStart = [this](double length) {
        return std::floor((m_surfaceLength - length) / 2);
    };
    if (resting != m_restingLength || m_surfaceLength != m_restingSurface) {
        m_fromLength = m_length;
        m_fromBias = m_restingLength < 0 ? restingStart(resting) - (m_surfaceLength - resting) / 2
                                         : m_bias;
        m_toBias = restingStart(resting) - (m_surfaceLength - resting) / 2;
        m_restingLength = resting;
        m_restingSurface = m_surfaceLength;
    }
    const double resize = m_applications->resizeProgress();
    const double distance = m_restingLength - m_fromLength;
    const double progress = resize < 1                ? resize
            : std::abs(distance) < 1e-9 ? 1
                                        : std::clamp((m_length - m_fromLength) / distance, 0.0, 1.0);
    m_bias = m_fromBias + (m_toBias - m_fromBias) * progress;
    m_start = (m_surfaceLength - m_length) / 2 + m_bias;
    if (resize >= 1)
        m_fromEnds = -1;
}

void DockWindow::layoutItems()
{
    if (m_windowRect.isEmpty())
        return;
    updateLayoutStart();
    const DockSettings::Direction direction = m_dock->settings().direction();
    const bool bottom = direction == DockSettings::Bottom;
    const bool left = direction == DockSettings::Left;

    m_drawn = DrawnDockMetrics::blend(m_fromMetrics, DrawnDockMetrics::of(m_metrics),
                                      m_applications->resizeProgress());
    const double drawnThickness = std::round(m_drawn.thickness);
    const double farPadding = m_drawn.outerPadding + drawnThickness - m_drawn.thickness;
    const double dotArea = m_drawn.edgePadding + m_drawn.dotSize + m_drawn.dotGap;
    const double itemSize = m_metrics.iconSize;
    const double itemScale = m_drawn.iconSize / itemSize;
    const double itemGrowth = (m_drawn.iconSize - itemSize) / 2;

    const int panelStart = int(std::floor(m_start + 0.5));
    const int panelEnd = int(std::floor(m_start + m_length + 0.5));
    const int gap = edgeGap();
    const int farSide = (bottom ? m_windowRect.height() : m_windowRect.width()) - gap;
    const base::Rect panel = bottom
            ? base::Rect(panelStart, farSide - int(drawnThickness), panelEnd - panelStart,
                         int(drawnThickness))
            : base::Rect(left ? gap : farSide - int(drawnThickness), panelStart,
                         int(drawnThickness), panelEnd - panelStart);

    const double origin = m_drawn.lengthPadding + m_start - panelStart;
    const double layoutOrigin = m_start + m_drawn.lengthPadding;
    const double settledStart = std::floor((m_surfaceLength - m_restingLength) / 2);
    const double settled = settledStart + m_metrics.lengthPadding;
    const auto along = [&](double position, double rest) {
        return place(layoutOrigin + position, settled + rest) - layoutOrigin;
    };
    const double across = bottom ? farPadding : left ? gap + dotArea : farPadding;

    const double slotsStart = layoutOrigin - m_drawn.iconGap / 2;
    const double slotsEnd = m_start + m_length - m_drawn.lengthPadding + m_drawn.iconGap / 2;
    const DockMagnification field = magnification(slotsStart, slotsEnd);
    const int rasterSize = field.isIdentity() ? 0 : magnifiedIconSize();
    const int reach = m_dock->settings().magnification()
            ? std::max(0, DockSettings::kLargestMagnifiedIconSize - m_metrics.iconSize)
            : 0;
    const double panelAlong = bottom ? panel.x() : panel.y();
    const auto drawnAlong = [&](double position) {
        return field.map(panelAlong + position) - panelAlong;
    };

    const auto slotRect = [&](double position) {
        const double start = origin + position;
        if (bottom)
            return ui::RectF {start - m_drawn.iconGap / 2, 0, itemSize + m_drawn.iconGap,
                              drawnThickness + gap};
        return ui::RectF {left ? -double(gap) : 0, start - m_drawn.iconGap / 2,
                          drawnThickness + gap, itemSize + m_drawn.iconGap};
    };
    const auto placeIcon = [&](DockIcon *icon, double position, double scale, double opacity) {
        const ui::RectF rest = slotRect(position);
        const ui::RectF restIcon = bottom ? ui::RectF {m_drawn.iconGap / 2, across, itemSize, itemSize}
                                          : ui::RectF {across, m_drawn.iconGap / 2, itemSize, itemSize};
        icon->setRestGeometry({rest.x + panel.x(), rest.y + panel.y(), rest.width, rest.height},
                              {rest.x + panel.x() + restIcon.x, rest.y + panel.y() + restIcon.y, itemSize, itemSize});
        ui::RectF slot = rest;
        ui::RectF iconRect = restIcon;
        double magnified = 1;
        if (!field.isIdentity()) {
            const double restStart = bottom ? rest.x : rest.y;
            const double restLength = bottom ? rest.width : rest.height;
            const double start = drawnAlong(restStart);
            const double length = drawnAlong(restStart + restLength) - start;
            magnified = (length - m_drawn.iconGap) / itemSize;
            if (std::abs(magnified - 1) < 1e-9)
                magnified = 1;
            if (bottom) {
                slot.x = start;
                slot.width = length;
                iconRect.x = (length - itemSize) / 2;
            } else {
                slot.y = start;
                slot.height = length;
                iconRect.y = (length - itemSize) / 2;
            }
        }
        if (bottom) {
            slot.y -= reach;
            slot.height += reach;
            iconRect.y += reach;
        } else {
            slot.width += reach;
            if (!left) {
                slot.x -= reach;
                iconRect.x += reach;
            }
        }
        icon->setGeometry({slot.x + panel.x(), slot.y + panel.y(), slot.width, slot.height});
        icon->setMagnification(magnified, rasterSize);
        icon->setIconRect(iconRect);
        icon->setPresentation(scale * itemScale, opacity);
        m_leavingIcons.erase(icon);
        const double dot = m_metrics.dotSize;
        const double dotGap = m_metrics.dotGap;
        if (bottom)
            icon->setDotRect({iconRect.x + (itemSize - dot) / 2, iconRect.y + itemSize + dotGap, dot, dot});
        else if (left)
            icon->setDotRect({iconRect.x - (dotGap + dot), iconRect.y + (itemSize - dot) / 2, dot, dot});
        else
            icon->setDotRect({iconRect.x + itemSize + dotGap, iconRect.y + (itemSize - dot) / 2, dot, dot});
    };

    const auto leave = [&](DockIcon *icon, double opacity) {
        if (m_leavingIcons.insert(icon).second)
            m_panel->raiseChild(icon);
        icon->setPresentationOpacity(opacity);
        icon->setEnabled(false);
    };
    const auto staying = [](const DockPresentation &presentation) {
        std::set<std::string> keys;
        for (const DockPresentation::Entry &entry : presentation.entries()) {
            if (!entry.leaving)
                keys.insert(entry.key);
        }
        return keys;
    };

    std::set<std::string> present;
    const std::set<std::string> stayingApplications = staying(*m_applications);
    for (const DockPresentation::Entry &entry : m_applications->entries()) {
        DockIcon *icon = applicationIcon(entry.key);
        if (!icon)
            continue;
        present.insert(entry.key);
        if (entry.leaving) {
            if (!stayingApplications.contains(entry.key)) {
                leave(icon, m_applications->appearance(entry));
                icon->setRunning(false);
            }
            continue;
        }
        const double position = along(m_applications->position(entry), m_applications->targetPosition(entry))
                + itemGrowth;
        placeIcon(icon, position, m_applications->iconScale(entry), m_applications->appearance(entry));
        icon->setEnabled(true);
    }
    for (auto it = m_applicationIcons.begin(); it != m_applicationIcons.end();) {
        if (present.contains(it->first)) {
            ++it;
            continue;
        }
        if (m_tooltipOwner == it->second)
            hideTooltip();
        if (m_menuIcon == it->second)
            m_menuIcon = nullptr;
        if (m_drop.hovered == it->second)
            setDropHovered(nullptr);
        retire(it->second);
        it = m_applicationIcons.erase(it);
    }

    std::set<uint64_t> windows;
    const double minimizedStart = m_minimizedStart;
    const double settledMinimizedStart = m_applications->targetLength() + m_applications->targetTrailingExtent();
    const std::set<std::string> stayingWindows = staying(*m_minimized);
    for (const DockPresentation::Entry &entry : m_minimized->entries()) {
        const uint64_t id = std::stoull(entry.key);
        DockIcon *icon = minimizedIcon(id);
        if (!icon)
            continue;
        windows.insert(id);
        const auto state = m_minimizedStates.find(id);
        const bool landed = state == m_minimizedStates.end() || state->second.landed;
        if (entry.leaving) {
            if (!stayingWindows.contains(entry.key))
                leave(icon, landed ? m_minimized->appearance(entry) : 0);
            continue;
        }
        const double position = along(minimizedStart + m_minimized->position(entry),
                                      settledMinimizedStart + m_minimized->targetPosition(entry))
                + itemGrowth;
        const bool revealed = state == m_minimizedStates.end() || !state->second.flownIn;
        placeIcon(icon, position, revealed ? m_minimized->iconScale(entry) : 1,
                  !landed ? 0 : revealed ? m_minimized->appearance(entry) : 1);
        icon->setEnabled(true);
    }
    for (auto it = m_minimizedIcons.begin(); it != m_minimizedIcons.end();) {
        if (windows.contains(it->first)) {
            ++it;
            continue;
        }
        if (m_tooltipOwner == it->second)
            hideTooltip();
        if (m_menuIcon == it->second)
            m_menuIcon = nullptr;
        if (m_drop.hovered == it->second)
            setDropHovered(nullptr);
        retire(it->second);
        m_minimizedStates.erase(it->first);
        it = m_minimizedIcons.erase(it);
    }

    placeIcon(m_trash,
              along(minimizedStart + m_minimized->length(), settledMinimizedStart + m_minimized->targetLength())
                      + itemGrowth,
              1, 1);

    const double lineLength = std::round(drawnThickness * 48 / 86);
    const double lineAcross = std::round((drawnThickness - lineLength) / 2);
    const double middle = (m_drawn.separatorExtent - m_drawn.iconGap) / 2 - 0.5;
    const auto lineRect = [&](double position) {
        const double start = field.isIdentity() ? origin + position : drawnAlong(origin + position);
        return bottom ? base::RectF(start, lineAcross, 1, lineLength)
                      : base::RectF(lineAcross, start, lineLength, 1);
    };
    m_divider = lineRect(along(m_applications->sectionPosition() + middle,
                               m_applications->targetSectionPosition() + middle));
    const double extent = m_applications->trailingExtent();
    m_trailingOpacity = 0.8 * std::clamp((extent - 2) / (m_drawn.separatorExtent - 2), 0.0, 1.0);
    m_trailingDivider = m_trailingOpacity > 0
            ? lineRect(along(m_applications->length() + middle * extent / m_drawn.separatorExtent,
                             m_applications->targetLength()
                                     + middle * m_applications->targetTrailingExtent()
                                             / m_metrics.sectionExtent()))
            : base::RectF();
    m_panel->setGeometry({0, 0, double(m_windowRect.width()), double(m_windowRect.height())});
    m_panel->requestPaint();
    base::Rect drawnPanel = panel;
    if (!field.isIdentity()) {
        const int start = int(std::lround(field.map(slotsStart) - (slotsStart - panelStart)));
        const int end = int(std::lround(field.map(slotsEnd) + (panelEnd - slotsEnd)));
        drawnPanel = bottom ? base::Rect(start, panel.y(), end - start, panel.height())
                            : base::Rect(panel.x(), start, panel.width(), end - start);
    }
    updatePanelRects(panel, drawnPanel);
    updateMinimizeTargets();
    if (m_tooltipOwner)
        showTooltip(m_tooltipOwner);
}

void DockWindow::retire(DockIcon *icon)
{
    // Not from inside the frame that laid it out: destroyed on the next turn.
    icon->setVisible(false);
    m_leavingIcons.erase(icon);
    m_retired.push_back(m_panel->takeChild(icon));
    if (m_retired.size() == 1) {
        core::mainLoop()->post([this, alive = m_alive] {
            if (*alive)
                m_retired.clear();
        });
    }
}

void DockWindow::announceGeometry()
{
    // The size taken for a drop gap lasts only as long as the drag, and what
    // lays itself out around the dock may be the drag's source.
    if (m_drop.gap || m_announcedGeometry == primaryGeometry())
        return;
    m_announcedGeometry = primaryGeometry();
    m_dock->notifyPrimaryGeometryChanged();
}

void DockWindow::updatePanelRects(const base::Rect &rest, const base::Rect &drawn)
{
    const bool moved = rest != m_restPanelRect;
    m_restPanelRect = rest;
    if (drawn != m_drawnPanelRect) {
        m_drawnPanelRect = drawn;
        updateGlass();
    }
    announceGeometry();
    if (!moved)
        return;
    updateInputRegion();
}

base::Rect DockWindow::minimizedSlot(int index, int count) const
{
    const double pitch = m_metrics.pitch();
    const double settled = m_applications->targetLength() + m_applications->targetTrailingExtent();
    const double before = settled + index * pitch;
    const double start = std::floor((m_surfaceLength - (settled + count * pitch + m_metrics.iconSize
                                                        + 2 * m_metrics.lengthPadding))
                                    / 2);
    const DockSettings::Direction direction = m_dock->settings().direction();
    const int thickness = m_metrics.thickness();
    const int size = m_metrics.iconSize;
    const int gap = edgeGap();
    if (direction == DockSettings::Bottom)
        return base::Rect(int(start + m_metrics.lengthPadding + before),
                          m_windowRect.height() - gap - thickness + m_metrics.outerPadding, size, size);
    return base::Rect(direction == DockSettings::Left
                              ? gap + m_metrics.dotAreaSize()
                              : m_windowRect.width() - gap - thickness + m_metrics.outerPadding,
                      int(start + m_metrics.lengthPadding + before), size, size);
}

base::Rect DockWindow::applicationSlot(double position) const
{
    const double settledStart = std::floor((m_surfaceLength - m_restingLength) / 2);
    const double alongStart = settledStart + m_metrics.lengthPadding + position;
    const DockSettings::Direction direction = m_dock->settings().direction();
    const int thickness = m_metrics.thickness();
    const int size = m_metrics.iconSize;
    const int gap = edgeGap();
    if (direction == DockSettings::Bottom)
        return base::Rect(int(alongStart),
                          m_windowRect.height() - gap - thickness + m_metrics.outerPadding, size, size);
    return base::Rect(direction == DockSettings::Left
                              ? gap + m_metrics.dotAreaSize()
                              : m_windowRect.width() - gap - thickness + m_metrics.outerPadding,
                      int(alongStart), size, size);
}

void DockWindow::updateMinimizeTargets()
{
    wm::Window *panel = m_window->wmWindow();
    if (!panel)
        return;
    wm::WindowManager *manager = m_shell->windowManager();
    TaskWindows &windows = m_shell->taskWindows();
    MinimizedWindows &minimized = m_dock->minimizedWindows();
    const bool area = m_dock->settings().minimizedArea();
    const base::Rect next = minimizedSlot(minimized.count(), minimized.count() + 1);
    for (uint64_t id : windows.windows()) {
        const TaskWindow *window = windows.window(id);
        if (!window || !window->isTaskListWindow())
            continue;
        base::Rect target;
        if (!area) {
            const std::string application = m_dock->applications().appIdForWindow(id);
            target = next;
            for (const DockPresentation::Entry &entry : m_applications->entries()) {
                if (entry.key == application && !entry.leaving)
                    target = applicationSlot(m_applications->targetPosition(entry));
            }
        } else if (window->minimized) {
            const auto &entries = minimized.entries();
            const auto entry = std::find_if(entries.begin(), entries.end(),
                                            [id](const MinimizedWindows::Entry &e) { return e.id == id; });
            const DockIcon *icon = minimizedIcon(id);
            if (entry == entries.end() || !icon)
                continue;
            if (!entry->landed) {
                target = minimizedSlot(int(entry - entries.begin()), minimized.count());
            } else {
                const ui::RectF rect = icon->restIconRect();
                target = base::Rect(int(std::lround(rect.x)), int(std::lround(rect.y)), int(rect.width),
                                    int(rect.height));
            }
        } else {
            target = next;
        }
        const base::Rect fitted = fitToWindow(target, window->geometry);
        if (fitted.isEmpty() || m_sentTargets[id] == fitted)
            continue;
        m_sentTargets[id] = fitted;
        if (wm::Window *wmWindow = manager->windowById(id))
            wmWindow->setMinimizedGeometry(panel, fitted);
    }
}

void DockWindow::handOffLanded()
{
    if (m_landed.empty())
        return;
    wm::WindowManager *manager = m_shell->windowManager();
    for (uint64_t id : std::exchange(m_landed, {})) {
        if (wm::Window *window = manager->windowById(id))
            window->endMinimizeHandoff();
    }
}

} // namespace cutefish::shell
