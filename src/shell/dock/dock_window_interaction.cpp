// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/dock/dock_window.h"

#include <algorithm>
#include <cmath>

#include "base/dbus.h"
#include "base/i18n.h"
#include "base/mime_database.h"
#include "base/strings.h"
#include "base/url.h"
#include "compositor/menu_model.h"
#include "input/drag.h"
#include "input/seat.h"
#include "shell/common/file_thread.h"
#include "shell/dock/dock.h"
#include "shell/dock/dock_icon.h"
#include "shell/dock/dock_presentation.h"
#include "shell/dock/dock_tooltip.h"
#include "shell/shell_window.h"
#include "ui/bubble_shape.h"
#include "ui/drag.h"
#include "ui/embedded_window.h"
#include "ui/event.h"
#include "ui/image.h"
#include "ui/menu.h"
#include "ui/theme.h"

using cutefish::i18n::tr;

namespace cutefish::shell {

namespace {

constexpr double kMenuArrowWidth = 14;
constexpr double kMenuArrowLength = 8;

} // namespace

void DockWindow::iconHovered(DockIcon *icon, bool hovered)
{
    if (hovered) {
        m_tooltipHide.stop();
        showTooltip(icon);
        return;
    }
    // The next item's enter arrives before this leave now that the slots
    // touch, and it already owns the tooltip.
    if (m_tooltipOwner == icon)
        m_tooltipHide.start();
}

void DockWindow::showTooltip(DockIcon *icon)
{
    if (m_menuOpen || icon->toolTipText().empty())
        return;
    m_tooltipOwner = icon;
    const ui::RectF slot = icon->geometry();
    const ui::RectF rect = icon->magnifiedIconRect();
    m_tooltip->show(icon->toolTipText(),
                    base::PointF(slot.x + rect.x + rect.width / 2, slot.y + rect.y + rect.height / 2),
                    farSide(icon), icon->bounceOffset());
}

double DockWindow::farSide(DockIcon *icon) const
{
    const bool full = m_magnifier.isEnabled() && (m_magnifier.isTracking() || m_magnifier.isHeld());
    const double reach = full ? magnifiedReach() : 0;
    const ui::RectF rest = icon->restIconRect();
    const ui::RectF slot = icon->geometry();
    const ui::RectF drawn = icon->magnifiedIconRect();
    const base::Rect &panel = m_restPanelRect;
    switch (m_dock->settings().direction()) {
    case DockSettings::Left:
        return std::ceil(std::max(double(panel.right()), full ? rest.right() + reach : slot.x + drawn.right()));
    case DockSettings::Right:
        return std::floor(std::min(double(panel.x()), full ? rest.x - reach : slot.x + drawn.x));
    case DockSettings::Bottom:
        break;
    }
    return std::floor(std::min(double(panel.y()), full ? rest.y - reach : slot.y + drawn.y));
}

void DockWindow::iconBounced(DockIcon *icon)
{
    if (icon == m_tooltipOwner && m_tooltip->isShown())
        showTooltip(icon);
}

void DockWindow::hideTooltip()
{
    m_tooltipHide.stop();
    m_tooltipOwner = nullptr;
    if (m_tooltip)
        m_tooltip->hide();
}

void DockWindow::iconClicked(DockIcon *icon, int button)
{
    switch (icon->kind()) {
    case DockIcon::Kind::Application:
        if (button == int(ui::MouseButton::Left))
            m_dock->applications().clicked(icon->key());
        else if (button == int(ui::MouseButton::Middle))
            m_dock->applications().openNewInstance(icon->key());
        break;
    case DockIcon::Kind::Minimized:
        if (button == int(ui::MouseButton::Left))
            m_dock->minimizedWindows().restore(icon->windowId());
        break;
    case DockIcon::Kind::Trash:
        if (button == int(ui::MouseButton::Left)) {
            callLaunchpad(m_shell->bus(), "hide");
            TrashMonitor::openTrash(m_shell->bus());
        }
        break;
    }
}

void DockWindow::iconMenuRequested(DockIcon *icon)
{
    auto menu = std::make_unique<ui::Menu>();
    base::dbus::Connection *bus = m_shell->bus();
    switch (icon->kind()) {
    case DockIcon::Kind::Application: {
        const std::string id = icon->key();
        if (id == DockModel::kLaunchpadId)
            return;
        DockModel &model = m_dock->applications();
        const int row = model.indexOf(id);
        if (row < 0)
            return;
        const DockItem &item = model.at(row);
        const std::shared_ptr<compositor::MenuModel> dockMenu = item.running ? model.dockMenu(id) : nullptr;
        if (dockMenu && !std::exchange(m_dockMenuPrepared, false)) {
            prepareDockMenu(id, dockMenu);
            return;
        }
        const auto windows = model.windows(id);
        for (const DockModel::WindowInfo &window : windows) {
            const size_t index = menu->addItem(window.title.empty() ? item.visibleName : window.title,
                                               [&model, id = window.id] { model.activateWindow(id); });
            menu->setItemChecked(index, window.active);
            menu->setItemMaximumWidth(index, 320);
        }
        if (!windows.empty())
            menu->addSeparator();
        if (dockMenu && addDockMenuEntries(*menu, dockMenu, 0))
            menu->addSeparator();
        if (item.pinnable() || (item.missing && item.isPinned)) {
            const bool pinned = item.isPinned;
            menu->addItem(pinned ? tr("Remove from Dock") : tr("Keep in Dock"), [&model, id, pinned] {
                pinned ? model.unPin(id) : model.pin(id);
            });
        }
        if (!item.running)
            menu->addItem(tr("Open"), [&model, id] { model.openNewInstance(id); });
        if (item.running)
            menu->addItem(tr("Quit"), [&model, id] { model.quit(id); });
        break;
    }
    case DockIcon::Kind::Minimized: {
        const uint64_t window = icon->windowId();
        MinimizedWindows &minimized = m_dock->minimizedWindows();
        menu->addItem(tr("Restore"), [&minimized, window] { minimized.restore(window); });
        menu->addItem(tr("Close"), [&minimized, window] { minimized.close(window); });
        break;
    }
    case DockIcon::Kind::Trash: {
        menu->addItem(tr("Open"), [bus] {
            callLaunchpad(bus, "hide");
            TrashMonitor::openTrash(bus);
        });
        const size_t empty = menu->addItem(tr("Empty Trash"), [bus] { TrashMonitor::emptyTrash(bus); });
        menu->setItemVisible(empty, !m_dock->trash().isEmpty());
        break;
    }
    }
    popupMenu(icon, std::move(menu));
}

void DockWindow::prepareDockMenu(const std::string &id, const std::shared_ptr<compositor::MenuModel> &dockMenu)
{
    auto show = [this, id] {
        m_dockMenuWait.stop();
        m_dockMenuUpdate = {};
        if (DockIcon *icon = applicationIcon(id)) {
            m_dockMenuPrepared = true;
            iconMenuRequested(icon);
        }
    };
    m_dockMenuUpdate = dockMenu->menuUpdated.connect([show](int32_t menuId) {
        if (menuId == 0)
            show();
    });
    m_dockMenuWait.onTimeout = show;
    m_dockMenuWait.start();
    dockMenu->prepare(0);
}

// True when there were any.
bool DockWindow::addDockMenuEntries(ui::Menu &menu, const std::shared_ptr<compositor::MenuModel> &model, int32_t id)
{
    const compositor::MenuItem *parent = model->item(id);
    if (!parent)
        return false;
    bool any = false;
    bool separatorPending = false;
    for (int32_t child : parent->children) {
        const compositor::MenuItem *entry = model->item(child);
        if (!entry || !entry->visible)
            continue;
        if (entry->separator) {
            separatorPending = any;
            continue;
        }
        if (std::exchange(separatorPending, false))
            menu.addSeparator();
        const std::string label = compositor::plainMenuLabel(entry->label);
        if (entry->hasSubmenu) {
            ui::Menu *submenu = menu.addSubmenu(label);
            addDockMenuEntries(*submenu, model, child);
        } else {
            std::weak_ptr<compositor::MenuModel> weak = model;
            const size_t index = menu.addItem(label, [weak, child] {
                if (std::shared_ptr<compositor::MenuModel> owner = weak.lock())
                    owner->activate(child);
            });
            menu.setItemEnabled(index, entry->enabled);
            if (entry->toggle != compositor::MenuItem::Toggle::None)
                menu.setItemChecked(index, entry->toggleState == 1);
        }
        any = true;
    }
    return any;
}

void DockWindow::popupMenu(DockIcon *icon, std::unique_ptr<ui::Menu> menu)
{
    hideTooltip();
    if (m_menu)
        m_menu->close();
    if (m_menuIcon)
        m_menuIcon->setMenuOpen(false);
    m_menu = std::move(menu);
    m_menuIcon = icon;
    m_menu->onClosed = [this] {
        m_menuOpen = false;
        m_magnifier.setHeld(false);
        resumeMagnification(true);
        DockIcon *icon = std::exchange(m_menuIcon, nullptr);
        if (!icon)
            return;
        icon->setMenuOpen(false);
        if (icon->isPointerOver())
            showTooltip(icon);
    };
    const DockSettings::Direction direction = m_dock->settings().direction();
    const ui::RectF slot = icon->geometry();
    const ui::RectF rect = icon->magnifiedIconRect();
    const ui::PointF iconStart {slot.x + rect.x, slot.y + rect.y};
    const ui::PointF centre {iconStart.x + rect.width / 2, iconStart.y + rect.height / 2};
    m_menu->setArrow(direction == DockSettings::Left    ? ui::BubbleShape::Edge::Left
                             : direction == DockSettings::Right ? ui::BubbleShape::Edge::Right
                                                                : ui::BubbleShape::Edge::Bottom,
                     centre);
    m_menu->setArrowSize(kMenuArrowWidth, kMenuArrowLength);
    m_menu->setContinuousCorners(true);
    if (const Shell::Output *output = m_shell->output({})) {
        base::Rect area = m_shell->workArea(*output);
        area.translate(-m_windowRect.x(), -m_windowRect.y());
        m_menu->setAvailableGeometry(
                ui::RectF {double(area.x()), double(area.y()), double(area.width()), double(area.height())});
    }
    ui::Window *window = m_window->window();
    const ui::SizeF size = m_menu->implicitSize(window);
    const double gap = ui::Theme::current().smallSpacing;
    const double far = farSide(icon);
    ui::PointF origin;
    if (direction == DockSettings::Left)
        origin = {far + gap, centre.y - size.height / 2};
    else if (direction == DockSettings::Right)
        origin = {far - size.width - gap, centre.y - size.height / 2};
    else
        origin = {centre.x - size.width / 2, far - size.height - gap};
    const double inset = m_menu->minimumEdgeLength() / 2;
    if (direction == DockSettings::Bottom)
        origin.x = std::min(iconStart.x, centre.x - inset);
    else
        origin.y = std::min(iconStart.y, centre.y - inset);
    m_magnifier.setHeld(true);
    m_menu->popupAt(window, {std::round(origin.x), std::round(origin.y)});
    m_menuOpen = m_menu->isOpen();
    icon->setMenuOpen(m_menuOpen);
    if (!m_menuOpen) {
        m_magnifier.setHeld(false);
        resumeMagnification(true);
    }
    updateInputRegion();
}

void DockWindow::iconDragRequested(DockIcon *icon)
{
    if (m_drag || icon->kind() == DockIcon::Kind::Trash)
        return;
    hideTooltip();
    icon->releasePress();
    double scale = 1;
    ui::Bitmap image = iconImage(icon, &scale);
    m_magnifier.setSuppressed(true);
    magnificationMoved();

    IconDrag drag;
    drag.kind = icon->kind();
    drag.key = icon->key();
    drag.window = icon->windowId();
    const bool application = drag.kind == DockIcon::Kind::Application;
    ui::DragSource source;
    source.mimeTypes = {application ? kApplicationMime : kWindowMime};
    source.data = [key = drag.key](const std::string &) { return key; };
    source.actions = ui::MoveAction;
    source.icon = std::move(image);
    source.iconScale = scale;
    const ui::RectF rect = icon->magnifiedIconRect();
    source.hotSpot = {rect.width / 2, rect.height / 2};
    drag.iconHeight = rect.height;
    // A drag the compositor never ran ends here; one it ran, in onDragEnded().
    source.onFinished = [this, alive = m_alive](ui::DropAction) {
        if (*alive && m_drag && !m_drag->started)
            finishIconDrag();
    };
    m_drag = drag;
    icon->setDragHidden(true);
    if (!icon->startDrag(std::move(source)) && m_drag && !m_drag->started)
        finishIconDrag();
}

ui::Bitmap DockWindow::iconImage(DockIcon *icon, double *scale) const
{
    *scale = pixelRatio();
    const ui::Image window = m_window->window()->grab();
    const ui::RectF slot = icon->geometry();
    const ui::RectF rect = icon->magnifiedIconRect();
    const int x = int(std::lround((slot.x + rect.x) * *scale));
    const int y = int(std::lround((slot.y + rect.y) * *scale));
    const int width = int(std::lround(rect.width * *scale));
    const int height = int(std::lround(rect.height * *scale));
    if (window.isNull() || x < 0 || y < 0 || x + width > window.width() || y + height > window.height()
        || width <= 0 || height <= 0)
        return {};
    return ui::Bitmap::copyOf(window.bits() + size_t(y) * window.width() + x, width, height,
                              size_t(window.width()) * 4, ui::Bitmap::Format::Argb32Premultiplied);
}

base::PointF DockWindow::iconCentre(DockIcon *icon) const
{
    const ui::RectF rect = icon->restIconRect();
    return {m_windowRect.x() + rect.x + rect.width / 2, m_windowRect.y() + rect.y + rect.height / 2};
}

double DockWindow::distanceOffDock(const base::PointF &position) const
{
    const base::Rect panel = primaryGeometry();
    switch (m_dock->settings().direction()) {
    case DockSettings::Left:
        return position.x() - (panel.x() + panel.width());
    case DockSettings::Right:
        return panel.x() - position.x();
    case DockSettings::Bottom:
        break;
    }
    return panel.y() - position.y();
}

void DockWindow::onDragStarted()
{
    input::DragController *drag = m_shell->seat()->drag();
    if (!m_drag || m_drag->started || drag->origin() != m_window->surface())
        return;
    m_drag->started = true;
    if (m_drag->kind != DockIcon::Kind::Application)
        return;
    DockIcon *icon = applicationIcon(m_drag->key);
    drag->setPlainIdleCursor(true);
    drag->setReturnsHome(true);
    if (icon)
        drag->setHome(iconCentre(icon));
    m_applications->beginDrag(m_dock->applications().indexOf(m_drag->key));
    if (!m_drop.anchored)
        anchorApplicationArea();
}

void DockWindow::onDragMoved(const base::PointF &location)
{
    if (!m_drag || !m_drag->started || m_drag->kind != DockIcon::Kind::Application)
        return;
    const DockModel &model = m_dock->applications();
    const int row = model.indexOf(m_drag->key);
    if (row < 0 || !model.at(row).isPinned || model.at(row).fixed)
        return;
    const double distance = distanceOffDock(location);
    const double thickness = m_metrics.thickness();
    if (distance > thickness * 1.5)
        m_drag->removes = true;
    else if (distance < thickness * 1.25)
        m_drag->removes = false;
    input::DragController *drag = m_shell->seat()->drag();
    drag->setReturnsHome(!m_drag->removes);
    drag->setVanishesOnDrop(m_drag->removes);
    if (m_drag->removes) {
        const base::Rect window = m_window->geometry();
        m_tooltip->showAbove(tr("Remove"), base::PointF(location.x() - window.x(),
                                                        location.y() - m_drag->iconHeight / 2 - window.y()));
    } else if (m_tooltip->isShown()) {
        hideTooltip();
    }
}

void DockWindow::onDragEnded()
{
    input::DragController *drag = m_shell->seat()->drag();
    const bool own = m_drag && m_drag->started && !m_drag->returning;
    if (own)
        hideTooltip();
    if (own && drag->isReturning()) {
        m_drag->removes = false;
        m_drag->returning = true;
        const DockModel &model = m_dock->applications();
        const int row = model.indexOf(m_drag->key);
        m_applications->setDropSlot(m_applications->draggedRow(), row >= 0 && model.at(row).isPinned);
    } else if (own) {
        finishIconDrag();
    } else if (!m_drag && m_applications->isDragging()) {
        m_applications->endDrag();
    }
    updateDropGap();
    resetDropTarget();
    if (!m_drag) {
        m_magnifier.setSuppressed(false);
        resumeMagnification(false);
    }
}

void DockWindow::finishIconDrag()
{
    if (!m_drag)
        return;
    const IconDrag drag = *m_drag;
    m_drag.reset();
    DockIcon *icon = drag.kind == DockIcon::Kind::Application ? applicationIcon(drag.key)
                                                              : minimizedIcon(drag.window);
    if (icon)
        icon->setDragHidden(false);
    m_magnifier.setSuppressed(false);
    resumeMagnification(false);
    if (drag.kind != DockIcon::Kind::Application)
        return;
    m_applications->endDrag();
    // Last: an icon of an application that is not running goes with its pin.
    if (drag.removeOnDrop)
        m_dock->applications().unPin(drag.key);
}

void DockWindow::resetDropTarget()
{
    setDropHovered(nullptr);
    const uint64_t generation = m_drop.generation + 1;
    m_drop = DropTarget();
    m_drop.generation = generation;
}

void DockWindow::dropEnter(ui::DropOffer *offer, const ui::PointF &position)
{
    m_magnifier.setSuppressed(true);
    magnificationMoved();
    // The same drag may come back in a new offer: what it carries is kept
    // until the drag ends.
    ++m_drop.generation;
    m_drop.offer = offer;
    m_drop.application = offer->hasMimeType(kApplicationMime);
    m_drop.desktop = offer->hasMimeType(kDesktopMime);
    m_drop.window = offer->hasMimeType(kWindowMime);
    m_drop.files = offer->hasMimeType("text/uri-list");
    const auto readId = [this, offer](const char *mime, bool desktop) {
        offer->read(mime, [this, alive = m_alive, generation = m_drop.generation, desktop](std::string data) {
            if (!*alive || generation != m_drop.generation)
                return;
            (desktop ? m_drop.desktopId : m_drop.appId) = std::string(base::strings::trimmed(data));
            if (m_drop.inArea) {
                enterApplicationArea();
                updateDropSlot();
                updateDropGap();
            }
        });
    };
    if (m_drop.application && m_drag && m_drag->kind == DockIcon::Kind::Application)
        m_drop.appId = m_drag->key;
    else if (m_drop.application)
        readId(kApplicationMime, false);
    if (m_drop.desktop)
        readId(kDesktopMime, true);
    if (m_drop.files && !m_drop.fileTypes && !m_drop.typesJob.isPending()) {
        offer->read("text/uri-list", [this, alive = m_alive, generation = m_drop.generation](std::string data) {
            if (!*alive || generation != m_drop.generation)
                return;
            std::vector<std::string> paths;
            for (const std::string &line : base::strings::split(data, '\n')) {
                const std::string trimmed(base::strings::trimmed(line));
                if (trimmed.empty() || trimmed.starts_with('#'))
                    continue;
                if (const auto path = base::url::toLocalFile(trimmed))
                    paths.push_back(*path);
            }
            m_drop.paths = paths;
            // Typing reads the files, which a slow mount must not make the
            // compositor wait for.
            m_drop.typesJob = runOnFileThread(
                    [paths] {
                        std::vector<std::string> types;
                        for (const std::string &path : paths)
                            types.push_back(base::mime::typeForFile(path));
                        return types;
                    },
                    [this](std::vector<std::string> types) {
                        m_drop.fileTypes = std::move(types);
                        dropMove(m_drop.position);
                    });
        });
    }
    dropMove(position);
}

uint32_t DockWindow::dropMove(const ui::PointF &position)
{
    m_drop.position = position;
    if (m_drop.application || m_drop.desktop) {
        const bool inside = inApplicationArea(position);
        if (inside && !m_drop.inArea) {
            m_drop.inArea = true;
            enterApplicationArea();
        } else if (!inside && m_drop.inArea) {
            m_drop.inArea = false;
            m_applications->setDropSlot(-1, false);
            m_drop.slot = -1;
            m_drop.hasLastAlong = false;
        }
        if (m_drop.inArea)
            updateDropSlot();
        updateDropGap();
    }
    setDropHovered(iconAt(position));
    DockIcon *opener = fileDropApplication();
    if (m_drop.hovered && m_drop.hovered->kind() == DockIcon::Kind::Application)
        m_drop.hovered->setDropHover(opener != nullptr);

    if (m_drop.inArea && m_drop.slot >= 0)
        return ui::MoveAction;
    if (opener)
        return ui::CopyAction;
    if (m_drop.hovered == m_trash && m_drop.files && m_drop.offer)
        return (m_drop.offer->sourceActions() & ui::MoveAction) ? ui::MoveAction : ui::CopyAction;
    if (m_drop.window && m_drop.hovered && m_drop.hovered->kind() == DockIcon::Kind::Minimized)
        return ui::MoveAction;
    return ui::NoAction;
}

void DockWindow::dropLeave()
{
    setDropHovered(nullptr);
    if (m_drop.inArea) {
        m_drop.inArea = false;
        m_applications->setDropSlot(-1, false);
        m_drop.slot = -1;
        m_drop.hasLastAlong = false;
        updateDropGap();
    }
    m_drop.offer = nullptr;
}

uint32_t DockWindow::dropAt(const ui::PointF &position)
{
    m_drop.position = position;
    uint32_t action = ui::NoAction;
    if (m_drop.inArea && m_drop.entered) {
        m_drop.hasLastAlong = false;
        updateDropSlot();
        // The dropped icon takes the gap's place in the count.
        const bool hadGap = m_drop.gap;
        m_drop.gap = false;
        if (m_drop.slot >= 0) {
            DockModel &model = m_dock->applications();
            const int row = m_applications->sourceRowForSlot(m_drop.slot);
            if (m_drop.desktop) {
                if (m_drop.pinned && m_drop.desktopId) {
                    if (model.insert(*m_drop.desktopId, row, m_applications->draggedRow()))
                        action = ui::MoveAction;
                } else if (m_drop.pinned && m_drop.offer) {
                    m_drop.offer->read(kDesktopMime, [this, alive = m_alive, row,
                                                      existing = m_applications->draggedRow()](std::string data) {
                        const std::string id(base::strings::trimmed(data));
                        if (*alive && !id.empty())
                            m_dock->applications().insert(id, row, existing);
                    });
                    action = ui::MoveAction;
                }
            } else if (m_drop.appId && model.dropApplication(*m_drop.appId, row, m_drop.pinned)) {
                action = ui::MoveAction;
            }
        }
        m_applications->endDrag();
        m_drop.entered = false;
        m_drop.anchored = false;
        m_drop.slot = -1;
        if (hadGap) {
            resize(true);
            announceGeometry();
        }
    } else if (DockIcon *opener = fileDropApplication()) {
        if (m_dock->applications().openFiles(opener->key(), m_drop.paths))
            action = ui::CopyAction;
    } else if (m_drop.hovered == m_trash && m_drop.files && m_drop.offer) {
        action = (m_drop.offer->sourceActions() & ui::MoveAction) ? ui::MoveAction : ui::CopyAction;
        m_drop.offer->read("text/uri-list", [bus = m_shell->bus()](std::string data) {
            std::vector<std::string> paths;
            for (const std::string &line : base::strings::split(data, '\n')) {
                const std::string trimmed(base::strings::trimmed(line));
                if (trimmed.empty() || trimmed.starts_with('#'))
                    continue;
                if (const auto path = base::url::toLocalFile(trimmed))
                    paths.push_back(*path);
            }
            if (!paths.empty() && bus)
                TrashMonitor::moveToTrash(paths, bus);
        });
    } else if (m_drop.window && m_drop.hovered && m_drop.hovered->kind() == DockIcon::Kind::Minimized) {
        if (m_reorderTimer.isActive()) {
            m_reorderTimer.stop();
            reorderMinimized();
        }
        action = ui::MoveAction;
    }
    setDropHovered(nullptr);
    m_drop.offer = nullptr;
    return action;
}

void DockWindow::setDropHovered(DockIcon *icon)
{
    if (icon == m_drop.hovered)
        return;
    if (m_drop.hovered)
        m_drop.hovered->setDropHover(false);
    m_raiseTimer.stop();
    m_reorderTimer.stop();
    m_drop.hovered = icon;
    if (!icon)
        return;
    switch (icon->kind()) {
    case DockIcon::Kind::Trash:
        icon->setDropHover(true);
        break;
    case DockIcon::Kind::Application: {
        const DockModel &model = m_dock->applications();
        const int row = model.indexOf(icon->key());
        if (row >= 0 && !model.at(row).fixed && !m_drop.application && !m_drop.window)
            m_raiseTimer.start();
        break;
    }
    case DockIcon::Kind::Minimized:
        if (m_drop.window && m_drag && m_drag->window != icon->windowId())
            m_reorderTimer.start();
        break;
    }
}

DockIcon *DockWindow::fileDropApplication() const
{
    DockIcon *icon = m_drop.hovered;
    if (!m_drop.files || !m_drop.fileTypes || m_drop.paths.empty() || !icon
        || icon->kind() != DockIcon::Kind::Application)
        return nullptr;
    return m_dock->applications().opensFiles(icon->key(), *m_drop.fileTypes) ? icon : nullptr;
}

void DockWindow::reorderMinimized()
{
    if (!m_drag || !m_drop.hovered || m_drop.hovered->kind() != DockIcon::Kind::Minimized)
        return;
    MinimizedWindows &minimized = m_dock->minimizedWindows();
    int from = -1;
    int to = -1;
    for (int row = 0; row < minimized.count(); ++row) {
        const uint64_t id = minimized.entries()[size_t(row)].id;
        if (id == m_drag->window)
            from = row;
        if (id == m_drop.hovered->windowId())
            to = row;
    }
    if (from >= 0 && to >= 0 && from != to)
        minimized.move(from, to);
}

DockIcon *DockWindow::iconAt(const ui::PointF &position) const
{
    for (DockIcon *icon : icons()) {
        if (!icon->isVisible() || !icon->isEnabled())
            continue;
        const ui::RectF rect = icon->restGeometry();
        if (position.x >= rect.x && position.x < rect.x + rect.width && position.y >= rect.y
            && position.y < rect.y + rect.height)
            return icon;
    }
    return nullptr;
}

bool DockWindow::inApplicationArea(const ui::PointF &position) const
{
    const bool bottom = m_dock->settings().direction() == DockSettings::Bottom;
    const double along = bottom ? position.x : position.y;
    const double origin = m_start + m_drawn.lengthPadding;
    return along >= origin - m_drawn.lengthPadding
            && along < origin + m_applications->length() + m_drawn.separatorExtent / 2;
}

void DockWindow::anchorApplicationArea()
{
    m_drop.referenceOrigin = m_start + m_drawn.lengthPadding
            - (m_applications->targetLength() + m_applications->targetTrailingExtent() - m_applications->length()
               - m_applications->trailingExtent())
                    / 2;
    m_drop.anchored = true;
    m_drop.hasLastAlong = false;
}

void DockWindow::enterApplicationArea()
{
    if ((m_drop.application && !m_drop.appId) || (m_drop.desktop && !m_drop.desktopId))
        return;
    const std::string appId = m_drop.appId.value_or(std::string());
    const std::string desktopId = m_drop.desktopId.value_or(std::string());
    if (!(m_drop.entered && m_applications->isDragging())) {
        if (m_drop.entered)
            m_applications->endDrag();
        if (!m_drop.anchored)
            anchorApplicationArea();
        DockModel &model = m_dock->applications();
        const int source = !desktopId.empty() ? model.desktopRow(desktopId) : model.indexOf(appId);
        m_drop.sourcePinned = source >= 0 && source < model.pinnedCount();
        m_applications->beginDrag(source, desktopId.empty());
    }
    m_drop.entered = true;
    m_drop.hasLastAlong = false;
}

void DockWindow::updateDropSlot()
{
    if (!m_drop.entered)
        return;
    const bool bottom = m_dock->settings().direction() == DockSettings::Bottom;
    const double along = (bottom ? m_drop.position.x : m_drop.position.y) - m_drop.referenceOrigin;
    // Layout animation must not change the slot under a stationary pointer.
    if (m_drop.hasLastAlong && std::abs(along - m_drop.lastAlong) < 0.5)
        return;
    m_drop.lastAlong = along;
    m_drop.hasLastAlong = true;

    const DockModel &model = m_dock->applications();
    const double pitch = m_drawn.iconSize + m_drawn.iconGap;
    const double iconGap = m_drawn.iconGap;
    const double separatorExtent = m_drawn.separatorExtent;
    const bool external = m_drop.desktopId && !m_drop.desktopId->empty();
    const int source = m_applications->removedRow();
    const int remaining = model.count() - (source >= 0 ? 1 : 0);
    const int pins = model.pinnedCount() - (source >= 0 && source < model.pinnedCount() ? 1 : 0);
    const int pinGap = m_applications->placeholder() >= 0 && m_applications->placeholderPinned() ? 1 : 0;
    const double externalShift = external ? pitch / 2 : 0;
    const int anchoredSlots = source >= 0 ? 1 : 0;
    const int openSlots = m_applications->placeholder() >= 0 ? 1 : 0;
    const double drawnShift = (anchoredSlots - openSlots) * pitch / 2;
    const bool pinsOnly = m_drop.sourcePinned || external;
    const double divider = pinsOnly ? separatorExtent : (separatorExtent - iconGap) / 2;
    const double boundary = (pins + pinGap) * pitch + divider + drawnShift;
    m_drop.pinned = along < boundary;
    if (!m_drop.pinned && pinsOnly) {
        m_drop.slot = -1;
        m_applications->setDropSlot(-1, false);
        return;
    }
    const int row = int(std::floor((along + (m_drop.pinned ? externalShift : -separatorExtent) + iconGap / 2) / pitch));
    m_drop.slot = m_drop.pinned ? std::max(model.firstMovableRow(), std::min(pins, row))
                                : std::max(pins, std::min(remaining, row));
    m_applications->setDropSlot(m_drop.slot, m_drop.pinned);
}

void DockWindow::updateDropGap()
{
    const bool gap = m_applications->placeholder() >= 0 && m_applications->removedRow() < 0;
    if (gap == m_drop.gap)
        return;
    m_drop.gap = gap;
    resize(true);
    announceGeometry();
    if (!m_drop.anchored)
        return;
    m_drop.referenceOrigin = (m_surfaceLength - m_restingLength) / 2 + m_toBias + m_metrics.lengthPadding
            + (gap ? m_metrics.pitch() / 2.0 : 0);
    m_drop.hasLastAlong = false;
}

} // namespace cutefish::shell
