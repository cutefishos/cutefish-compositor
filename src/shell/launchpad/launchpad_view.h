// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_VIEW_H_
#define CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_VIEW_H_

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/signal.h"
#include "base/timer.h"
#include "shell/launchpad/launchpad_metrics.h"
#include "shell/launchpad/launchpad_tween.h"
#include "ui/scroller.h"
#include "ui/view.h"

namespace cutefish::ui {
class Label;
class Menu;
} // namespace cutefish::ui

namespace cutefish::shell {

class ApplicationBadges;
class Launchpad;
class LaunchpadCell;
class LaunchpadFolderView;
class LaunchpadPageIndicator;
class LaunchpadSearchField;
class LaunchpadWallpaper;

class LaunchpadView final : public ui::View, private ui::Scroller::Target
{
public:
    // Carries the desktop id as UTF-8.
    static constexpr const char *kDesktopIdMimeType = "application/x-cutefish-desktop-id";

    LaunchpadView(Launchpad &launchpad, LaunchpadWallpaper &wallpaper, ApplicationBadges &badges);
    ~LaunchpadView() override;

    void setMargins(const LaunchpadMetrics::Margins &margins);
    // In window coordinates.
    void setAvailableGeometry(std::optional<ui::RectF> geometry) { m_availableGeometry = geometry; }
    const LaunchpadMetrics &metrics() const { return m_metrics; }

    void setShown(bool shown);
    void preloadIcons();

    int pageTotal() const { return int(m_pages.size()); }
    int currentPage() const { return m_currentPage; }
    double contentX() const { return m_contentX; }
    // An index into the model's rows; -1 for none.
    int selectedIndex() const { return m_selectedIndex; }
    void setSelectedIndex(int index);
    bool isSearching() const;
    LaunchpadSearchField *searchField() const { return m_search; }
    LaunchpadCell *cell(const std::string &id) const;
    LaunchpadCell *anyCell(const std::string &id) const;
    // Null when absent.
    LaunchpadCell *cellAt(int row) const;
    // In window coordinates.
    ui::RectF cellRect(int row) const;
    const std::string &draggedId() const { return m_draggedId; }
    bool animatesReorder() const;
    ui::Menu *menu() const;
    bool isMenuOpen() const;
    // In window coordinates.
    std::vector<ui::PointF> indicatorDots() const;
    bool isNotFoundVisible() const;
    LaunchpadFolderView *folderView() const { return m_folder; }
    // Empty for none.
    const std::string &mergeTargetId() const { return m_mergeTargetId; }

    void moveSelection(LaunchpadDirection direction);
    void launchSelected();
    void openFolder(const std::string &folder, bool editName = false);
    void closeFolder();
    void goToPage(int page);
    void scrollNextPage();
    void scrollPreviousPage();
    void jumpToPage(int page);
    void stopSwipe();

    void cellClicked(LaunchpadCell *cell, ui::MouseButton button, ui::PointF windowPosition);
    bool startCellDrag(LaunchpadCell *cell, ui::View *icon);

    base::Signal<> dismissRequested;

protected:
    bool acceptsFocus() const override;
    void layout() override;
    void paint(ui::Painter &painter) override;
    bool keyPress(const ui::KeyEvent &event) override;
    bool scroll(const ui::ScrollEvent &event) override;
    bool canDrop(const ui::DropOffer &offer) const override;
    void dragEnter(const ui::DragEvent &event) override;
    uint32_t dragUpdate(const ui::DragEvent &event) override;
    void dragLeave() override;
    uint32_t drop(const ui::DragEvent &event) override;

private:
    class Pages;
    class Page;
    friend class Pages;

    ui::Scroller::Axis horizontalAxis() const override;
    ui::Scroller::Axis verticalAxis() const override;
    ui::PointF position() const override;
    void applyPosition(ui::PointF position) override;
    void movingChanged(bool moving) override;

    LaunchpadMetrics computeMetrics() const;
    void syncCells();
    void syncPages();
    void placePages();
    void updateSelection();
    void setContentX(double x);
    double pageX(int page) const;
    int nearestPage() const;
    void settleSwipe(int page);
    void slideTo(int page);
    void clampCurrentPage();
    void startSettle(std::chrono::milliseconds duration);
    bool settling() const;
    void applySearch(const std::string &text);
    void flushSearch();
    void searchTextChanged();
    bool searchKey(uint32_t key);
    void handleWheel(const ui::ScrollEvent &event);
    void openMenu(LaunchpadCell *cell, ui::PointF windowPosition);
    void endDrag();
    void requestMove(int to);
    void applyMove();
    void requestMerge();
    void merge(const std::string &id, const std::string &target);
    void updateDragTarget(ui::PointF position);
    bool updateFolderDragTarget(ui::PointF position);
    void setMergeTarget(const std::string &id);
    void setHovered(const std::string &id);
    void updateBackdrop();
    void retranslate();
    void retire(std::unique_ptr<ui::View> view);

    void backgroundPressed(ui::PointF position);
    void backgroundMoved(ui::PointF position);
    void backgroundReleased(ui::PointF position);

    Launchpad &m_launchpad;
    LaunchpadWallpaper &m_wallpaper;
    ApplicationBadges &m_badges;
    LaunchpadMetrics::Margins m_margins;
    LaunchpadMetrics m_metrics;
    bool m_shown = false;

    Pages *m_pagesView = nullptr;
    std::vector<Page *> m_pages;
    std::map<std::string, LaunchpadCell *> m_cells;
    LaunchpadSearchField *m_search = nullptr;
    LaunchpadPageIndicator *m_indicator = nullptr;
    ui::Label *m_notFound = nullptr;
    LaunchpadFolderView *m_folder = nullptr;

    int m_currentPage = -1;
    // -1 outside a search.
    int m_pageBeforeSearch = -1;
    double m_contentX = 0;
    bool m_swiping = false;
    std::chrono::steady_clock::time_point m_settleUntil;
    LaunchpadTween m_slide;
    std::unique_ptr<ui::Scroller> m_scroller;
    int m_selectedIndex = -1;

    double m_wheelAccumulated = 0;
    base::Timer m_wheelReset;
    base::Timer m_searchTimer;

    bool m_backgroundPressed = false;
    bool m_flicking = false;
    ui::PointF m_pressPosition;
    double m_pressContentX = 0;
    std::vector<std::pair<std::chrono::steady_clock::time_point, double>> m_flickSamples;

    std::optional<ui::RectF> m_availableGeometry;
    std::unique_ptr<ui::Menu> m_menu;
    std::string m_menuId;

    std::string m_draggedId;
    std::string m_hoveredId;
    bool m_reorderPending = false;
    base::Timer m_reorderTimer;
    base::Timer m_reorderAnimation;
    int m_edge = 0;
    base::Timer m_edgeTimer;
    std::string m_pendingMoveId;
    int m_pendingMoveTo = -1;
    // Empty for the grid.
    std::string m_pendingMoveFolder;
    std::string m_mergeTargetId;

    base::Timer m_preloadTimer;
    std::vector<std::string> m_preloadedNames;
    std::string m_preloadedKey;

    std::vector<std::unique_ptr<ui::View>> m_retired;
    std::vector<base::Subscription> m_subscriptions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_VIEW_H_
