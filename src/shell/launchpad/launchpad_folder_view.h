// Copyright 2026 Sacha Grandcolas
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_FOLDER_VIEW_H_
#define CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_FOLDER_VIEW_H_

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"
#include "base/timer.h"
#include "shell/launchpad/launchpad_metrics.h"
#include "shell/launchpad/launchpad_tween.h"
#include "ui/scroller.h"
#include "ui/view.h"

namespace cutefish::ui {
class TextField;
} // namespace cutefish::ui

namespace cutefish::shell {

class ApplicationBadges;
class LaunchpadCell;
class LaunchpadModel;
class LaunchpadPageIndicator;
class LaunchpadView;
class LaunchpadWallpaper;

class LaunchpadFolderView final : public ui::View, private ui::Scroller::Target
{
public:
    static constexpr int kMaximumColumns = 4;
    static constexpr std::chrono::milliseconds kOpenDuration {280};
    static constexpr std::chrono::milliseconds kCloseDuration {220};

    LaunchpadFolderView(LaunchpadView *view, LaunchpadModel &model, ApplicationBadges &badges,
                        LaunchpadWallpaper &wallpaper);
    ~LaunchpadFolderView() override;

    void setMetrics(const LaunchpadMetrics &metrics);

    // `from` is in the view's coordinates; empty grows in place.
    void open(const std::string &folder, bool animate, ui::RectF from = {});
    void close();
    // No longer counts as open while it shrinks.
    void dismiss();
    // Keeps the cells until close(): a drag out of the panel started on one of
    // them.
    void detach();
    bool isOpen() const { return !m_folder.empty() && !m_detached && !m_closing; }
    bool isDetached() const { return m_detached; }
    const std::string &folder() const { return m_folder; }

    void sync();
    void editName();
    ui::TextField *nameField() const;

    LaunchpadCell *cell(const std::string &id) const;
    // In the view's coordinates.
    ui::RectF panelRect() const;
    // In the view's coordinates.
    ui::RectF cellRect(int index) const;
    // `position` is in the view's coordinates; -1 outside the folder's
    // applications.
    int indexAt(ui::PointF position) const;
    // -1 or 1 over the empty side of the panel a drag pages towards, 0
    // elsewhere or with no page that way.
    int edgeAt(ui::PointF position) const;

    int pageTotal() const;
    int currentPage() const { return m_currentPage; }
    void goToPage(int page);
    // In the view's coordinates.
    std::vector<ui::PointF> indicatorDots() const;

protected:
    bool acceptsPointer() const override { return true; }
    void layout() override;
    void paint(ui::Painter &painter) override;
    void pointerPress(const ui::PointerEvent &event) override;
    bool scroll(const ui::ScrollEvent &event) override;

private:
    class NameField;
    class Panel;
    friend class NameField;
    friend class Panel;

    ui::Scroller::Axis horizontalAxis() const override;
    ui::Scroller::Axis verticalAxis() const override;
    ui::PointF position() const override;
    void applyPosition(ui::PointF position) override;
    void movingChanged(bool moving) override;

    int columns() const;
    int pageSize() const;
    double padding() const;
    double nameGap() const;
    double indicatorGap() const;
    // None for one page.
    double indicatorHeight() const;
    int nameFontSize() const;
    double nameHeight() const;
    // In the view's coordinates.
    ui::RectF gridRect() const;
    // 0 closed, 1 open.
    void setProgress(double progress);
    int visibleRows() const;
    double titleHeight() const;
    // In the panel's coordinates.
    ui::PointF slotPosition(int index) const;
    void placeCells();
    void setContentX(double x);
    int nearestPage() const;
    void setCurrentPage(int page, bool slide);
    void resetPages();
    void handleWheel(const ui::ScrollEvent &event);

    void panelPressed(ui::PointF position);
    void panelMoved(ui::PointF position);
    void panelReleased(ui::PointF position);
    void commitName();
    void retire(std::unique_ptr<ui::View> view);

    LaunchpadView *m_view;
    LaunchpadModel &m_model;
    ApplicationBadges &m_badges;
    LaunchpadWallpaper &m_wallpaper;
    LaunchpadMetrics m_metrics;
    std::string m_folder;
    bool m_detached = false;
    bool m_closing = false;
    int m_closeSerial = 0;
    ui::RectF m_from;
    double m_progress = 1;
    Panel *m_panel = nullptr;
    NameField *m_name = nullptr;
    ui::View *m_grid = nullptr;
    LaunchpadPageIndicator *m_indicator = nullptr;
    std::map<std::string, LaunchpadCell *> m_cells;
    std::vector<std::string> m_order;
    LaunchpadTween m_fade;

    int m_currentPage = 0;
    // A panel's width per page.
    double m_contentX = 0;
    bool m_swiping = false;
    LaunchpadTween m_slide;
    double m_wheelAccumulated = 0;
    base::Timer m_wheelReset;
    bool m_panelPressed = false;
    bool m_flicking = false;
    ui::PointF m_pressPosition;
    double m_pressContentX = 0;
    std::vector<std::pair<std::chrono::steady_clock::time_point, double>> m_flickSamples;
    std::unique_ptr<ui::Scroller> m_scroller;
    std::vector<std::unique_ptr<ui::View>> m_retired;
    base::Subscription m_wallpaperChanged;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_FOLDER_VIEW_H_
