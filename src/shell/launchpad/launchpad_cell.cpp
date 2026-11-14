// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/launchpad/launchpad_cell.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

#include "shell/common/count_badge.h"
#include "shell/launchpad/launchpad_view.h"
#include "ui/icon.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/theme.h"

namespace cutefish::shell {

// How far a press moves before it starts a drag.
static constexpr double kStartDragDistance = 10;

static int jsRound(double value)
{
    return int(std::floor(value + 0.5));
}

static double srgbToLinear(double value)
{
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

// Ink.on(): white over what white keeps 3:1 contrast with, black otherwise.
static ui::Color inkOn(ui::Color background, double alpha)
{
    const double luminance = 0.2126 * srgbToLinear(background.r / 255.0)
            + 0.7152 * srgbToLinear(background.g / 255.0) + 0.0722 * srgbToLinear(background.b / 255.0);
    const bool white = 1.05 / (luminance + 0.05) >= 3.0;
    return (white ? ui::Color {255, 255, 255, 255} : ui::Color {0, 0, 0, 255}).withAlpha(alpha);
}

// FishUI.IconItem, the only part of the cell that takes the pointer.
class LaunchpadCell::Icon final : public ui::ThemeIcon
{
public:
    explicit Icon(LaunchpadCell *cell)
        : m_cell(cell)
    {
    }

    bool acceptsPointer() const override { return true; }

protected:
    void paint(ui::Painter &painter) override
    {
        // A folder's preview draws in its place.
        if (!m_cell->isFolder())
            ui::ThemeIcon::paint(painter);
    }
    void pointerPress(const ui::PointerEvent &event) override { m_cell->iconPressed(event); }
    void pointerMove(const ui::PointerEvent &event) override { m_cell->iconMoved(event); }
    void pointerRelease(const ui::PointerEvent &event) override { m_cell->iconReleased(event); }

private:
    LaunchpadCell *m_cell;
};

// A folder's icon: a plate holding the first of its applications' icons,
// three by three. It draws dimmed while the cell is pressed.
class LaunchpadCell::FolderPreview final : public ui::View
{
public:
    static constexpr size_t kColumns = 3;

    explicit FolderPreview(LaunchpadCell *cell)
        : m_cell(cell)
    {
        setName("launchpad-folder-preview");
    }

    void setIcons(const std::vector<LaunchpadItem> &children)
    {
        const size_t count = std::min(children.size(), kColumns * kColumns);
        while (m_icons.size() > count) {
            takeChild(m_icons.back());
            m_icons.pop_back();
        }
        while (m_icons.size() < count)
            m_icons.push_back(addChild<ui::ThemeIcon>());
        for (size_t i = 0; i < count; ++i) {
            const std::string &name = children[i].iconName;
            m_icons[i]->setIconName(name.empty() ? "application-x-desktop" : name);
        }
        requestLayout();
    }

    // The icon of the folder's application at `index`; null past the
    // preview's nine.
    ui::ThemeIcon *icon(size_t index) const { return index < m_icons.size() ? m_icons[index] : nullptr; }

    void updatePressed()
    {
        const double opacity = m_cell->isPressDimmed() ? 0.6 : 1;
        for (ui::ThemeIcon *icon : m_icons)
            icon->setOpacity(opacity);
        requestPaint();
    }

protected:
    void layout() override
    {
        const double size = geometry().width;
        const double padding = jsRound(size * 0.12);
        const double gap = std::max(1, jsRound(size * 0.04));
        const double icon = std::floor((size - 2 * padding - (kColumns - 1) * gap) / kColumns);
        for (size_t i = 0; i < m_icons.size(); ++i) {
            m_icons[i]->setSize(icon);
            m_icons[i]->setGeometry({padding + double(i % kColumns) * (icon + gap),
                                     padding + double(i / kColumns) * (icon + gap), icon, icon});
        }
    }

    void paint(ui::Painter &painter) override
    {
        const ui::RectF bounds = localRect();
        // Darkened rather than lightened while pressed.
        const bool dimmed = m_cell->isPressDimmed();
        const ui::Color plate = dimmed ? ui::Color {0, 0, 0, 255} : ui::Color {255, 255, 255, 255};
        painter.drawRoundedRect(bounds, {double(jsRound(bounds.width * 0.22)), 0, ui::Corners::Continuous},
                                plate.withAlpha(dimmed ? 0.3 : 0.22));
    }

private:
    LaunchpadCell *m_cell;
    std::vector<ui::ThemeIcon *> m_icons;
};

LaunchpadCell::LaunchpadCell(LaunchpadView *view, std::string id)
    : m_view(view)
    , m_id(std::move(id))
{
    setName("launchpad-cell:" + m_id);
    m_icon = addChild<Icon>(this);
    m_label = addChild<ui::Label>();
    m_label->setColor(ui::Color {255, 255, 255, 255});
    m_label->setWrap(true);
    m_label->setMaximumLineCount(2);
    m_label->setElide(ui::Label::Elide::Right);
    m_label->setTextAlignment(ui::Label::TextAlignment::Center);
}

LaunchpadCell::~LaunchpadCell() = default;

void LaunchpadCell::setItem(const LaunchpadItem &item)
{
    if (item.isFolder()) {
        // The preview stands in for the icon, which only takes the pointer.
        if (!m_preview) {
            m_preview = addChild<FolderPreview>(this);
            requestLayout();
        }
        m_preview->setIcons(item.children);
        m_childCount = item.children.size();
    }
    const std::string iconName = item.isFolder() ? "folder" : item.iconName;
    if (iconName != m_iconName) {
        m_iconName = iconName;
        m_icon->setIconName(m_iconName.empty() ? "application-x-desktop" : m_iconName);
        if (m_mask)
            m_mask->setIconName(m_icon->iconName());
    }
    if (item.name != m_label->text())
        m_label->setText(item.name);
    if (item.newInstalled != m_newInstalled) {
        m_newInstalled = item.newInstalled;
        requestPaint();
    }
}

void LaunchpadCell::setCount(int64_t count)
{
    if (!m_countBadge) {
        if (count <= 0)
            return;
        m_countBadge = addChild<CountBadge>();
        m_countBadge->setHeightRatio(CountBadge::kLaunchpadRatio);
    }
    m_countBadge->setCount(count);
    updateCountBadge();
}

void LaunchpadCell::updateCountBadge()
{
    if (!m_countBadge)
        return;
    raiseChild(m_countBadge);
    m_countBadge->setIconRect(iconRect());
    m_countBadge->setVisible(m_countBadge->count() > 0 && !m_dragging);
}

void LaunchpadCell::setMetrics(const LaunchpadMetrics &metrics)
{
    if (metrics == m_metrics)
        return;
    m_metrics = metrics;
    m_icon->setSize(metrics.iconSize);
    if (m_mask)
        m_mask->setSize(metrics.iconSize);
    m_label->setFontSize(metrics.labelPixelSize);
    requestLayout();
    requestPaint();
}

void LaunchpadCell::setSelected(bool selected)
{
    if (selected == m_selected)
        return;
    m_selected = selected;
    requestPaint();
}

void LaunchpadCell::setDragging(bool dragging)
{
    if (dragging == m_dragging)
        return;
    m_dragging = dragging;
    m_pressed = false;
    m_icon->setVisible(!dragging);
    m_label->setVisible(!dragging);
    if (m_preview)
        m_preview->setVisible(!dragging);
    updateMask();
    updateCountBadge();
    requestPaint();
}

void LaunchpadCell::setMenuOpen(bool open)
{
    if (open == m_menuOpen)
        return;
    m_menuOpen = open;
    updateMask();
}

void LaunchpadCell::setMergeTarget(bool target)
{
    if (target == m_mergeTarget)
        return;
    m_mergeTarget = target;
    requestPaint();
}

void LaunchpadCell::setBackdrop(ui::Color color)
{
    if (color == m_backdrop)
        return;
    m_backdrop = color;
    if (m_selected)
        requestPaint();
}

void LaunchpadCell::updateMask()
{
    // A folder's preview dims itself.
    if (m_preview) {
        m_preview->updatePressed();
        return;
    }
    // Made on the first press, then only hidden: rasterising it for every
    // icon would cost each icon twice.
    if (isPressDimmed() && !m_mask) {
        m_mask = addChild<ui::ThemeIcon>(m_icon->iconName(), m_metrics.iconSize);
        m_mask->setColor(ui::Color {0, 0, 0, 255});
        m_mask->setOpacity(0.4);
        m_mask->setGeometry(iconRect());
        updateCountBadge();
    }
    if (m_mask)
        m_mask->setVisible(isPressDimmed());
}

void LaunchpadCell::moveTo(const ui::PointF &position, bool animate)
{
    const ui::RectF current = geometry();
    const auto place = [this](double x, double y) {
        setGeometry({x, y, double(m_metrics.cellWidth), double(m_metrics.cellHeight)});
    };
    if (!animate || !window()) {
        m_moveX.stop();
        m_moveY.stop();
        place(position.x, position.y);
        return;
    }
    if (m_moveX.isRunning() && m_moveX.target() == position.x && m_moveY.target() == position.y)
        return;
    if (current.x == position.x && current.y == position.y && !isMoving())
        return;
    m_moveX.start(window(), current.x, position.x, [this, place](double x) { place(x, geometry().y); });
    m_moveY.start(window(), current.y, position.y, [this, place](double y) { place(geometry().x, y); });
}

void LaunchpadCell::finishMove()
{
    if (m_moveX.isRunning())
        m_moveX.finish();
    if (m_moveY.isRunning())
        m_moveY.finish();
    if (m_folderTween.isRunning())
        m_folderTween.finish();
}

void LaunchpadCell::playFolderAnimation(bool created)
{
    if (!m_preview)
        return;
    if (created) {
        m_folderTween.start(window(), 0.6, 1, kFolderCreatedDuration, easing::outBack,
                            [this](double scale) { m_preview->setTransform({{}, scale}); });
        return;
    }
    // The application that went in is the folder's last.
    const size_t added = m_childCount - 1;
    m_folderTween.start(window(), 0, 1, kFolderAddedDuration, easing::linear, [this, added](double t) {
        // Up by 12% and back, exactly at rest at the end.
        const double scale = t < 1 ? 1 + 0.12 * std::sin(std::numbers::pi * t) : 1;
        m_preview->setTransform({{}, scale});
        if (ui::ThemeIcon *icon = m_preview->icon(added))
            icon->setOpacity(t);
    });
}

ui::Bitmap LaunchpadCell::dragImage(double scale)
{
    if (!m_preview)
        return ui::themeIconBitmap(m_iconName.empty() ? "application-x-desktop" : m_iconName,
                                   m_metrics.iconSize, scale);
    // Not dimmed by the press the drag starts from.
    const bool pressed = std::exchange(m_pressed, false);
    m_preview->updatePressed();
    ui::Bitmap image = m_preview->grab(scale);
    m_pressed = pressed;
    m_preview->updatePressed();
    return image;
}

ui::RectF LaunchpadCell::iconRect(const LaunchpadMetrics &metrics)
{
    const double size = metrics.iconSize;
    return {double(jsRound((metrics.cellWidth - size) / 2)), double(metrics.cellPaddingV), size, size};
}

static ui::RectF labelArea(const LaunchpadMetrics &metrics)
{
    const double width = metrics.selectionWidth - metrics.cellSpacing;
    return {double(jsRound((metrics.cellWidth - width) / 2)),
            double(metrics.cellPaddingV + metrics.iconSize + metrics.cellSpacing), width,
            double(metrics.labelLineHeight * 2)};
}

ui::RectF LaunchpadCell::selectionRect() const
{
    if (!m_selected || m_dragging)
        return {};
    const ui::RectF icon = iconRect();
    const ui::RectF label = labelArea(m_metrics);
    // Follows the painted text, not the two-line reservation.
    const double contentHeight = m_label->heightForWidth(label.width);
    const double y = std::max(0.0, icon.y - m_metrics.selectionPadding);
    const double height = std::min(m_metrics.cellHeight - y,
                                   label.y + contentHeight + jsRound(m_metrics.iconSize * 0.14) - y);
    return {double(jsRound((m_metrics.cellWidth - m_metrics.selectionWidth) / 2.0)), y,
            double(m_metrics.selectionWidth), height};
}

ui::RectF LaunchpadCell::newMarkRect() const
{
    if (!m_newInstalled || m_dragging || m_label->text().empty())
        return {};
    const ui::RectF label = labelArea(m_metrics);
    const double size = std::max(5, jsRound(m_metrics.iconSize * 0.06));
    // The first line's advance and centre, as onLineLaidOut reports them.
    const std::optional<ui::Label::LineGeometry> line = m_label->lineGeometry(0);
    if (!line)
        return {};
    const double lineWidth = line->width;
    const double lineCentre = line->top + line->height / 2;
    const double x = label.x + (label.width - lineWidth) / 2 - size
            - std::max(2, jsRound(m_metrics.cellSpacing / 3.0)) - 2;
    return {x, label.y + lineCentre - size / 2, size, size};
}

void LaunchpadCell::layout()
{
    const ui::RectF icon = iconRect();
    m_icon->setGeometry(icon);
    if (m_mask)
        m_mask->setGeometry(icon);
    if (m_preview)
        m_preview->setGeometry(icon);
    updateCountBadge();
    const ui::RectF label = labelArea(m_metrics);
    // Lines the label's height cannot hold go before the maximum:
    // with a font taller than 1.3 em the two-line area holds one.
    const double line = m_label->ascent() + m_label->descent();
    m_label->setMaximumLineCount(std::clamp(int(std::floor(label.height / std::max(1.0, line))), 1, 2));
    m_label->setGeometry(label);
}

void LaunchpadCell::paint(ui::Painter &painter)
{
    if (m_mergeTarget && !m_dragging) {
        const ui::RectF icon = iconRect();
        const double grow = jsRound(icon.width * 0.12);
        const ui::RectF plate = icon.adjusted(-grow, -grow, grow, grow);
        painter.drawRoundedRect(plate, {double(jsRound(plate.width * 0.22)), 0, ui::Corners::Continuous},
                                inkOn(m_backdrop, 0.25));
    }
    const ui::RectF selection = selectionRect();
    if (selection.width > 0)
        painter.drawRoundedRect(selection,
                                {double(jsRound(m_metrics.iconSize * 0.2)), 0, ui::themeCorners()},
                                inkOn(m_backdrop, 0.2));
    const ui::RectF mark = newMarkRect();
    if (mark.width > 0)
        painter.drawSmoothRectangle(mark, mark.height / 2, ui::Theme::current().highlight);
}

void LaunchpadCell::iconPressed(const ui::PointerEvent &event)
{
    m_pressed = true;
    m_dragArmed = event.button == ui::MouseButton::Left;
    m_pressPoint = event.position;
    updateMask();
}

void LaunchpadCell::iconMoved(const ui::PointerEvent &event)
{
    if (!m_dragArmed || !m_pressed || m_dragging)
        return;
    const double dx = event.position.x - m_pressPoint.x;
    const double dy = event.position.y - m_pressPoint.y;
    if (dx * dx + dy * dy < kStartDragDistance * kStartDragDistance)
        return;
    m_dragArmed = false;
    m_view->startCellDrag(this, m_icon);
}

void LaunchpadCell::iconReleased(const ui::PointerEvent &event)
{
    const bool wasPressed = m_pressed;
    m_pressed = false;
    m_dragArmed = false;
    updateMask();
    if (!wasPressed || m_dragging)
        return;
    // A click: the release over the icon it pressed.
    const ui::RectF bounds = m_icon->localRect();
    if (event.position.x < bounds.x || event.position.y < bounds.y
        || event.position.x >= bounds.x + bounds.width || event.position.y >= bounds.y + bounds.height)
        return;
    m_view->cellClicked(this, event.button, m_icon->mapToWindow(event.position));
}

} // namespace cutefish::shell
