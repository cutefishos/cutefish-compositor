// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_CELL_H_
#define CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_CELL_H_

#include <chrono>
#include <cstdint>
#include <string>

#include "shell/launchpad/launchpad_metrics.h"
#include "shell/launchpad/launchpad_model.h"
#include "shell/launchpad/launchpad_tween.h"
#include "ui/bitmap.h"
#include "ui/view.h"

namespace cutefish::ui {
class Label;
class ThemeIcon;
} // namespace cutefish::ui

namespace cutefish::shell {

class CountBadge;
class LaunchpadView;

class LaunchpadCell final : public ui::View
{
public:
    LaunchpadCell(LaunchpadView *view, std::string id);
    ~LaunchpadCell() override;

    const std::string &id() const { return m_id; }
    bool isFolder() const { return LaunchpadItem::isFolderId(m_id); }

    void setItem(const LaunchpadItem &item);
    // 0 for none.
    void setCount(int64_t count);
    CountBadge *countBadge() const { return m_countBadge; }
    const std::string &iconName() const { return m_iconName; }
    void setMetrics(const LaunchpadMetrics &metrics);
    void setSlot(int page, int index) { m_page = page; m_index = index; }
    int page() const { return m_page; }
    int index() const { return m_index; }

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    void setDragging(bool dragging);
    void setMenuOpen(bool open);
    void setMergeTarget(bool target);
    bool isMergeTarget() const { return m_mergeTarget; }
    bool isPressDimmed() const { return (m_pressed || m_menuOpen) && !m_dragging; }
    void setBackdrop(ui::Color color);

    void moveTo(const ui::PointF &position, bool animate);
    bool isMoving() const { return m_moveX.isRunning() || m_moveY.isRunning(); }
    void finishMove();

    static constexpr std::chrono::milliseconds kFolderCreatedDuration {360};
    static constexpr std::chrono::milliseconds kFolderAddedDuration {300};
    void playFolderAnimation(bool created);
    bool isPlayingFolderAnimation() const { return m_folderTween.isRunning(); }

    // Device pixels at `scale`.
    ui::Bitmap dragImage(double scale);

    // In the cell's coordinates.
    ui::RectF iconRect() const { return iconRect(m_metrics); }
    static ui::RectF iconRect(const LaunchpadMetrics &metrics);
    // In the cell's coordinates; empty while hidden.
    ui::RectF selectionRect() const;
    // In the cell's coordinates; empty while hidden.
    ui::RectF newMarkRect() const;

    void iconPressed(const ui::PointerEvent &event);
    void iconMoved(const ui::PointerEvent &event);
    void iconReleased(const ui::PointerEvent &event);

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    class Icon;
    class FolderPreview;

    void updateMask();
    void updateCountBadge();

    LaunchpadView *m_view;
    std::string m_id;
    std::string m_iconName;
    bool m_newInstalled = false;
    LaunchpadMetrics m_metrics;
    int m_page = 0;
    int m_index = 0;
    Icon *m_icon = nullptr;
    FolderPreview *m_preview = nullptr;
    ui::ThemeIcon *m_mask = nullptr;
    CountBadge *m_countBadge = nullptr;
    ui::Label *m_label = nullptr;
    ui::Color m_backdrop {0, 0, 0, 255};
    bool m_selected = false;
    bool m_dragging = false;
    bool m_menuOpen = false;
    bool m_mergeTarget = false;
    bool m_pressed = false;
    bool m_dragArmed = false;
    ui::PointF m_pressPoint;
    LaunchpadSpring m_moveX;
    LaunchpadSpring m_moveY;
    LaunchpadTween m_folderTween;
    size_t m_childCount = 0;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_CELL_H_
