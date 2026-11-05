// Copyright 2026 Sacha Grandcolas
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/launcher/launcher_folder_view.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "base/strings.h"
#include "core/main_loop.h"
#include "shell/common/application_badges.h"
#include "shell/launcher/launcher_cell.h"
#include "shell/launcher/launcher_model.h"
#include "shell/launcher/launcher_view.h"
#include "shell/launcher/launcher_wallpaper.h"
#include "ui/keys.h"
#include "ui/painter.h"
#include "ui/text_field.h"
#include "ui/window.h"

namespace cutefish::shell {

static const ui::Color kWhite {255, 255, 255, 255};

static int jsRound(double value)
{
    return int(std::floor(value + 0.5));
}

// The folder's name over its applications, edited in place. Return keeps
// the edit, Escape drops it and closes the folder.
class LauncherFolderView::NameField final : public ui::TextField
{
public:
    explicit NameField(LauncherFolderView *owner)
        : m_owner(owner)
    {
        setFrameVisible(false);
        setHorizontallyCentered(true);
        setTextColor(kWhite);
        setSelectByMouse(true);
        setMaximumLength(64);
    }

    // The name as it was when the editing began.
    std::string original;

protected:
    void focusOut() override
    {
        ui::TextField::focusOut();
        m_owner->commitName();
    }
    bool keyPress(const ui::KeyEvent &event) override
    {
        switch (event.key) {
        case ui::key::Escape:
            // The name as it was, and the folder closes.
            setText(original);
            m_owner->dismiss();
            return true;
        case ui::key::Return:
        case ui::key::KeypadEnter:
            m_owner->commitName();
            m_owner->m_view->setFocus();
            return true;
        default:
            return ui::TextField::keyPress(event);
        }
    }

private:
    LauncherFolderView *m_owner;
};

// The panel the name and the applications sit on; it grows and shrinks
// about its centre.
class LauncherFolderView::Panel final : public ui::View
{
public:
    explicit Panel(LauncherFolderView *owner)
        : m_owner(owner)
    {
        setName("launcher-folder-panel");
    }

protected:
    void paint(ui::Painter &painter) override
    {
        painter.drawSmoothRectangle(localRect(), jsRound(m_owner->m_metrics.iconSize * 0.3), kWhite.withAlpha(0.16));
    }

private:
    LauncherFolderView *m_owner;
};

LauncherFolderView::LauncherFolderView(LauncherView *view, LauncherModel &model, ApplicationBadges &badges,
                                       LauncherWallpaper &wallpaper)
    : m_view(view)
    , m_model(model)
    , m_badges(badges)
    , m_wallpaper(wallpaper)
{
    setName("launcher-folder");
    // The pages slide in layers of their own, which go above whatever is
    // painted flat; the folder's layer comes after theirs. It also fades as
    // one.
    setLayered(true);
    setVisible(false);
    m_wallpaperChanged = m_wallpaper.changed.connect([this] { requestPaint(); });
    m_panel = addChild<Panel>(this);
    m_name = m_panel->addChild<NameField>(this);
    m_grid = m_panel->addChild<ui::View>();
    m_grid->setClipsChildren(true);
}

LauncherFolderView::~LauncherFolderView()
{
    *m_alive = false;
}

void LauncherFolderView::setMetrics(const LauncherMetrics &metrics)
{
    if (metrics == m_metrics)
        return;
    m_metrics = metrics;
    m_name->setFontSize(jsRound(metrics.labelPixelSize * 1.4));
    for (const auto &[id, cell] : m_cells)
        cell->setMetrics(metrics);
    requestLayout();
}

void LauncherFolderView::open(const std::string &folder, bool animate, ui::RectF from)
{
    if (!m_folder.empty())
        close();
    m_folder = folder;
    m_detached = false;
    m_from = from;
    m_scrollY = 0;
    setVisible(true);
    sync();
    setProgress(0);
    m_fade.start(animate ? window() : nullptr, 0, 1, kOpenDuration, easing::outCubic,
                 [this](double value) { setProgress(value); });
}

void LauncherFolderView::dismiss()
{
    if (!isOpen())
        return;
    const bool focused = m_name->hasFocus();
    commitName();
    m_closing = true;
    // Nothing in it takes the pointer while it shrinks.
    setEnabled(false);
    if (focused)
        m_view->setFocus();
    const int serial = ++m_closeSerial;
    std::weak_ptr<bool> alive = m_alive;
    m_fade.start(window(), m_progress, 0, kCloseDuration, easing::outCubic, [this, alive, serial](double value) {
        setProgress(value);
        if (value > 0)
            return;
        // Not from within the animation's own update.
        core::mainLoop()->post([this, alive, serial] {
            if (!alive.expired() && m_closing && serial == m_closeSerial)
                close();
        });
    });
}

void LauncherFolderView::setProgress(double progress)
{
    m_progress = progress;
    setOpacity(progress);
    // From the icon's size and place to the panel's.
    const ui::RectF panel = panelRect();
    if (panel.width <= 0)
        return;
    const double start = m_from.width > 0 ? m_from.width / panel.width : 0.9;
    ui::PointF translation;
    if (m_from.width > 0) {
        translation = {(m_from.x + m_from.width / 2 - (panel.x + panel.width / 2)) * (1 - progress),
                       (m_from.y + m_from.height / 2 - (panel.y + panel.height / 2)) * (1 - progress)};
    }
    m_panel->setTransform({translation, start + (1 - start) * progress});
}

void LauncherFolderView::close()
{
    if (m_folder.empty())
        return;
    const bool focused = m_name->hasFocus();
    commitName();
    m_folder.clear();
    m_detached = false;
    m_order.clear();
    for (const auto &[id, cell] : m_cells)
        retire(cell->parent()->takeChild(cell));
    m_cells.clear();
    m_fade.stop();
    m_closing = false;
    setEnabled(true);
    m_from = {};
    setProgress(1);
    setVisible(false);
    if (focused)
        m_view->setFocus();
}

void LauncherFolderView::detach()
{
    if (m_folder.empty())
        return;
    commitName();
    m_detached = true;
    setVisible(false);
}

void LauncherFolderView::sync()
{
    if (m_folder.empty() || m_detached)
        return;
    const LauncherItem *folder = m_model.item(m_folder);
    if (!folder || !folder->isFolder()) {
        close();
        return;
    }
    if (!m_name->hasFocus()) {
        m_name->setText(folder->name);
        m_name->original = folder->name;
    }

    std::set<std::string> present;
    m_order.clear();
    // The cells a reorder displaces slide as the grid's do; new ones appear
    // in place.
    const bool animate = m_view->animatesReorder();
    for (size_t i = 0; i < folder->children.size(); ++i) {
        const LauncherItem &item = folder->children[i];
        present.insert(item.id);
        m_order.push_back(item.id);
        LauncherCell *cell = this->cell(item.id);
        const bool placed = cell != nullptr;
        if (!cell) {
            cell = m_grid->addChild<LauncherCell>(m_view, item.id);
            m_cells[item.id] = cell;
        }
        cell->setItem(item);
        cell->setCount(m_badges.count(item.id));
        cell->setMetrics(m_metrics);
        cell->setSlot(0, int(i));
        cell->setDragging(item.id == m_view->draggedId());
        cell->moveTo(slotPosition(int(i)), animate && placed);
    }
    for (auto it = m_cells.begin(); it != m_cells.end();) {
        if (present.count(it->first)) {
            ++it;
            continue;
        }
        retire(it->second->parent()->takeChild(it->second));
        it = m_cells.erase(it);
    }
    requestLayout();
    requestPaint();
}

void LauncherFolderView::editName()
{
    if (!isOpen())
        return;
    m_name->setFocus();
    m_name->selectAll();
}

ui::TextField *LauncherFolderView::nameField() const
{
    return m_name;
}

void LauncherFolderView::commitName()
{
    if (m_folder.empty())
        return;
    const std::string name(base::strings::trimmed(m_name->text()));
    // A folder keeps a name.
    if (name.empty()) {
        m_name->setText(m_name->original);
        return;
    }
    m_name->original = name;
    m_model.renameFolder(m_folder, name);
}

LauncherCell *LauncherFolderView::cell(const std::string &id) const
{
    const auto it = m_cells.find(id);
    return it != m_cells.end() ? it->second : nullptr;
}

int LauncherFolderView::columns() const
{
    return std::clamp(m_metrics.columns, 1, kMaximumColumns);
}

int LauncherFolderView::visibleRows() const
{
    const int rows = std::max(1, (int(m_order.size()) + columns() - 1) / columns());
    // A row of the grid stays clear for the name and the margins.
    return std::clamp(rows, 1, std::max(1, m_metrics.rows - 1));
}

double LauncherFolderView::titleHeight() const
{
    return m_metrics.searchFieldHeight + m_metrics.cellSpacing;
}

ui::RectF LauncherFolderView::panelRect() const
{
    const double padding = 2 * m_metrics.cellSpacing;
    const double width = columns() * m_metrics.cellWidth + 2 * padding;
    const double height = titleHeight() + visibleRows() * m_metrics.cellHeight + 2 * padding;
    const base::Rect grid = m_metrics.gridArea;
    return {double(grid.x() + jsRound((grid.width() - width) / 2)),
            double(grid.y() + jsRound((grid.height() - height) / 2)), width, height};
}

ui::RectF LauncherFolderView::gridRect() const
{
    const ui::RectF panel = m_panel->geometry();
    const ui::RectF grid = m_grid->geometry();
    return {panel.x + grid.x, panel.y + grid.y, grid.width, grid.height};
}

ui::RectF LauncherFolderView::cellRect(int index) const
{
    if (index < 0)
        return {};
    const ui::RectF grid = gridRect();
    return {grid.x + (index % columns()) * m_metrics.cellWidth,
            grid.y + (index / columns()) * m_metrics.cellHeight - m_scrollY, double(m_metrics.cellWidth),
            double(m_metrics.cellHeight)};
}

int LauncherFolderView::indexAt(ui::PointF position) const
{
    const ui::RectF grid = gridRect();
    if (!grid.contains(position))
        return -1;
    const int column = int((position.x - grid.x) / m_metrics.cellWidth);
    const int row = int((position.y - grid.y + m_scrollY) / m_metrics.cellHeight);
    const int index = row * columns() + column;
    return column < columns() && index < int(m_order.size()) ? index : -1;
}

void LauncherFolderView::layout()
{
    const ui::RectF panel = panelRect();
    const double padding = 2 * m_metrics.cellSpacing;
    const double nameWidth = std::min(panel.width - 2 * padding, double(m_metrics.searchFieldWidth));
    m_panel->setGeometry(panel);
    m_name->setGeometry({double(jsRound((panel.width - nameWidth) / 2)), padding, nameWidth,
                         double(m_metrics.searchFieldHeight)});
    m_grid->setGeometry({padding, padding + titleHeight(), double(columns() * m_metrics.cellWidth),
                         double(visibleRows() * m_metrics.cellHeight)});
    setScroll(m_scrollY);
    // The panel's size may have changed under a running growth.
    setProgress(m_progress);
}

ui::PointF LauncherFolderView::slotPosition(int index) const
{
    return {double((index % columns()) * m_metrics.cellWidth),
            double((index / columns()) * m_metrics.cellHeight) - m_scrollY};
}

void LauncherFolderView::placeCells()
{
    for (size_t i = 0; i < m_order.size(); ++i) {
        // A cell already sliding to its slot keeps sliding: a layout must
        // not cut a reorder's animation short.
        if (LauncherCell *cell = this->cell(m_order[i]))
            cell->moveTo(slotPosition(int(i)), cell->isMoving());
    }
}

void LauncherFolderView::setScroll(double y)
{
    const int rows = (int(m_order.size()) + columns() - 1) / columns();
    const double maximum = std::max(0, rows - visibleRows()) * double(m_metrics.cellHeight);
    m_scrollY = std::clamp(y, 0.0, maximum);
    placeCells();
}

void LauncherFolderView::paint(ui::Painter &painter)
{
    // The launcher's own backdrop, so the grid does not show through the
    // panel, a shade darker.
    const ui::RectF bounds = localRect();
    if (m_wallpaper.isPicture()) {
        painter.drawImage(m_wallpaper.image(), bounds);
        painter.fillRect(bounds, ui::Color {0, 0, 0, 102});
    } else {
        painter.fillRect(bounds, m_wallpaper.color());
    }
    painter.fillRect(bounds, ui::Color {0, 0, 0, 38});
}

void LauncherFolderView::pointerPress(const ui::PointerEvent &event)
{
    if (!panelRect().contains(event.position)) {
        dismiss();
        return;
    }
    // A press on the panel ends the editing.
    if (m_name->hasFocus())
        m_view->setFocus();
}

bool LauncherFolderView::scroll(const ui::ScrollEvent &event)
{
    // Rows past the panel's scroll; the pages behind never turn.
    const double delta = event.value120Y != 0 ? event.value120Y / 120.0 * m_metrics.cellHeight : event.deltaY;
    setScroll(m_scrollY + delta);
    return true;
}

void LauncherFolderView::retire(std::unique_ptr<ui::View> view)
{
    if (!view)
        return;
    m_retired.push_back(std::move(view));
    std::weak_ptr<bool> alive = m_alive;
    core::mainLoop()->post([this, alive] {
        if (!alive.expired())
            m_retired.clear();
    });
}

} // namespace cutefish::shell
