// Copyright 2026 Sacha Grandcolas
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHER_LAUNCHER_FOLDER_VIEW_H_
#define CUTEFISH_SHELL_LAUNCHER_LAUNCHER_FOLDER_VIEW_H_

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"
#include "shell/launcher/launcher_metrics.h"
#include "shell/launcher/launcher_tween.h"
#include "ui/view.h"

namespace cutefish::ui {
class TextField;
} // namespace cutefish::ui

namespace cutefish::shell {

class ApplicationBadges;
class LauncherCell;
class LauncherModel;
class LauncherView;
class LauncherWallpaper;

// An open folder, over the whole launcher in a layer of its own, above the
// pages' layers: the launcher's backdrop again, hiding the grid, the folder's
// name, which can be edited, and below it a panel with its applications in
// rows of the grid's cells. The panel grows out of the folder's icon as the
// backdrop and the name fade in, and shrinks back into it as the folder
// closes. A press outside the panel or Escape closes it.
class LauncherFolderView final : public ui::View
{
public:
    // The most columns a folder's panel has.
    static constexpr int kMaximumColumns = 4;
    static constexpr std::chrono::milliseconds kOpenDuration {280};
    static constexpr std::chrono::milliseconds kCloseDuration {220};

    LauncherFolderView(LauncherView *view, LauncherModel &model, ApplicationBadges &badges,
                       LauncherWallpaper &wallpaper);
    ~LauncherFolderView() override;

    void setMetrics(const LauncherMetrics &metrics);

    // `from` is the folder's icon in the view's coordinates, which the panel
    // grows out of when `animate`; empty to grow in place.
    void open(const std::string &folder, bool animate, ui::RectF from = {});
    // Writes the name being edited and closes at once.
    void close();
    // Writes the name being edited and closes after shrinking back into the
    // icon. It no longer counts as open meanwhile.
    void dismiss();
    // Hides at once but keeps the cells until close(): a drag out of the
    // panel started on one of them.
    void detach();
    bool isOpen() const { return !m_folder.empty() && !m_detached && !m_closing; }
    bool isDetached() const { return m_detached; }
    const std::string &folder() const { return m_folder; }

    // Follows the model; a folder that went closes.
    void sync();
    // Selects the name for editing.
    void editName();
    ui::TextField *nameField() const;

    LauncherCell *cell(const std::string &id) const;
    // The panel, in the view's coordinates.
    ui::RectF panelRect() const;
    // Where an application's cell rests, in the view's coordinates.
    ui::RectF cellRect(int index) const;
    // The index of the slot under a point in the view's coordinates; -1
    // outside the folder's applications.
    int indexAt(ui::PointF position) const;

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

    int columns() const;
    // The grid of cells, in the view's coordinates.
    ui::RectF gridRect() const;
    // 0 closed, 1 open: the backdrop's opacity and the panel's growth.
    void setProgress(double progress);
    int visibleRows() const;
    double nameHeight() const;
    // The name and the gap below it.
    double titleHeight() const;
    // Where the cell of an application rests, in the grid of cells.
    ui::PointF slotPosition(int index) const;
    void placeCells();
    void setScroll(double y);
    void commitName();
    void retire(std::unique_ptr<ui::View> view);

    LauncherView *m_view;
    LauncherModel &m_model;
    ApplicationBadges &m_badges;
    LauncherWallpaper &m_wallpaper;
    LauncherMetrics m_metrics;
    std::string m_folder;
    bool m_detached = false;
    bool m_closing = false;
    // Which dismissal a finished shrink belongs to.
    int m_closeSerial = 0;
    ui::RectF m_from;
    double m_progress = 1;
    Panel *m_panel = nullptr;
    NameField *m_name = nullptr;
    // The cells, clipped to the panel's rows.
    ui::View *m_grid = nullptr;
    std::map<std::string, LauncherCell *> m_cells;
    std::vector<std::string> m_order;
    double m_scrollY = 0;
    LauncherTween m_fade;
    std::vector<std::unique_ptr<ui::View>> m_retired;
    base::Subscription m_wallpaperChanged;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHER_LAUNCHER_FOLDER_VIEW_H_
