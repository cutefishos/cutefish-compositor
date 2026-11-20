// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SWITCHER_SWITCHER_VIEW_H_
#define CUTEFISH_SWITCHER_SWITCHER_VIEW_H_

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/geometry.h"
#include "base/timer.h"

#include "base/signal.h"
#include "switcher/application_info.h"
#include "ui/bitmap.h"
#include "wm/window_cycle_controller.h"

namespace cutefish::render {
class Renderer;
class Texture;
} // namespace cutefish::render

namespace cutefish::toolkit {
class PanelTexture;
}

namespace cutefish::ui {
class EmbeddedWindow;
}

namespace cutefish::decoration {
class ShadowNode;
class Theme;
} // namespace cutefish::decoration

namespace cutefish::scene {
class Node;
}

namespace cutefish::wm {
class WindowManager;
}

namespace cutefish::switcher {

class SwitcherContent;

class SwitcherView : public wm::WindowCycleView
{
public:
    // Logical pixels. Cells shrink together until the row fits the output.
    static constexpr double kCellSize = 108;
    static constexpr double kIconRatio = 96.0 / 108;
    static constexpr double kSelectionRatio = 98.0 / 108;
    static constexpr double kPadding = 16;
    static constexpr double kVerticalPadding = 18;
    static constexpr double kScreenMargin = 40;
    static constexpr int kRadius = 36;
    // At full size; it shrinks with the cells.
    static constexpr double kSelectionRadius = 22;

    SwitcherView(scene::Node *container, wm::WindowManager *manager,
                 const decoration::Theme *theme);
    ~SwitcherView() override;

    // Builds the view tree ahead of the first switch, then renders the icon of every
    // window that maps, one at a time while idle.
    void preload();
    // preload() once `delay` has passed; the view going first cancels it.
    void preloadAfter(std::chrono::milliseconds delay);

    static constexpr int kWarmDelayMs = 200;

    void showWindowCycle(const std::vector<wm::Window *> &entries, int selected,
                         const std::string &output) override;
    void selectWindowCycleEntry(int index) override;
    void hideWindowCycle() override;
    bool windowCycleViewContains(const base::PointF &location) const override;
    int windowCycleEntryAt(const base::PointF &location) const override;

    ui::Bitmap renderImage();
    render::Texture *panelTexture(render::Renderer &renderer);
    void releaseRenderResources();
    base::Rect bounds() const { return m_bounds; }
    // In global logical coordinates; empty with nothing selected.
    base::Rect selectionBounds() const;
    int selectionRadius() const { return base::roundToInt(kSelectionRadius * m_cellSize / kCellSize); }
    const decoration::Theme *theme() const { return m_theme; }

private:
    void ensureWindow();
    void markDirty();
    std::pair<base::Rect, double> outputFor(const std::string &output) const;
    void layout(const std::vector<ApplicationInfo> &entries, int selected, const base::Rect &area,
                double scale);
    double rowY() const { return (m_bounds.height() - m_cellSize) / 2; }
    // In device pixels: icons are made once at it and scaled into shrunken cells.
    static int iconPixels(double scale);
    void followIconTheme();
    void warmNext();

    wm::WindowManager *m_manager;
    const decoration::Theme *m_theme;
    ApplicationInfoCache m_applications;
    std::string m_iconTheme;
    std::deque<uint64_t> m_warmQueue;
    base::Timer m_warmTimer;
    base::Timer m_preloadTimer;
    std::unique_ptr<ui::EmbeddedWindow> m_window;
    std::unique_ptr<toolkit::PanelTexture> m_panelTexture;
    SwitcherContent *m_content = nullptr;
    decoration::ShadowNode *m_shadow = nullptr;
    scene::Node *m_panel = nullptr;
    base::Rect m_bounds;
    double m_rowX = 0;
    double m_cellSize = kCellSize;
    int m_count = 0;
    int m_selected = -1;
    bool m_shown = false;
    bool m_dirty = false;
    base::Subscription m_windowAdded;
};

} // namespace cutefish::switcher

#endif // CUTEFISH_SWITCHER_SWITCHER_VIEW_H_
