// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_DOCK_DOCK_WINDOW_H_
#define CUTEFISH_SHELL_DOCK_DOCK_WINDOW_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/region.h"
#include "base/signal.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "render/glass.h"
#include "shell/dock/dock_icon.h"
#include "shell/dock/dock_magnification.h"
#include "shell/dock/dock_metrics.h"
#include "shell/dock/minimized_windows.h"
#include "ui/bitmap.h"
#include "ui/geometry.h"
#include "shell/shell.h"

namespace cutefish::compositor {
class MenuModel;
}

namespace cutefish::ui {
class DropOffer;
class Menu;
struct PointerEvent;
class View;
} // namespace cutefish::ui

namespace cutefish::shell {

class Dock;
class DockIcon;
class DockPresentation;
class DockTooltip;
class ShellWindow;

class DockWindow : public Component
{
public:
    DockWindow(Shell *shell, std::unique_ptr<Dock> dock);
    ~DockWindow() override;

    Dock &dock() { return *m_dock; }
    ShellWindow *window() const { return m_window.get(); }
    // In the window's coordinates, without magnification.
    base::Rect panelRect() const { return m_restPanelRect; }
    base::Rect primaryGeometry() const;
    base::Rect drawnPanelRect() const { return m_drawnPanelRect; }
    base::Rect windowRect(int thickness) const;
    int edgeGap() const;
    int windowAcross(int thickness) const;
    render::GlassParams glass() const;
    double cornerRadius() const;

    std::vector<DockIcon *> icons() const;
    DockIcon *applicationIcon(const std::string &id) const;
    DockIcon *minimizedIcon(uint64_t window) const;
    DockIcon *trashIcon() const { return m_trash; }
    // In dock coordinates.
    base::Rect minimizeTarget(uint64_t window) const
    {
        const auto it = m_sentTargets.find(window);
        return it != m_sentTargets.end() ? it->second : base::Rect();
    }
    // In panel coordinates; empty while absent.
    base::RectF divider() const { return m_divider; }
    base::RectF trailingDivider() const { return m_trailingDivider; }
    DockTooltip *tooltip() const { return m_tooltip.get(); }
    ui::Menu *openMenu() const { return m_menuOpen ? m_menu.get() : nullptr; }
    const DockMagnifier &magnifier() const { return m_magnifier; }

    static constexpr const char *kApplicationMime = "application/x-cutefish-dock-app-id";
    static constexpr const char *kWindowMime = "application/x-cutefish-dock-window-id";
    static constexpr const char *kDesktopMime = "application/x-cutefish-desktop-id";

    DockPresentation *applicationPresentation() const { return m_applications.get(); }
    DockPresentation *minimizedPresentation() const { return m_minimized.get(); }
    bool isDraggingIcon() const { return m_drag.has_value(); }

    void iconHovered(DockIcon *icon, bool hovered);
    void iconDragRequested(DockIcon *icon);
    void iconClicked(DockIcon *icon, int button);
    void iconBounced(DockIcon *icon);
    void iconMenuRequested(DockIcon *icon);
    void hideTooltip();

private:
    class MagnificationFrames;
    class Panel;

    struct IconDrag {
        DockIcon::Kind kind;
        std::string key;
        uint64_t window = 0;
        double iconHeight = 0;
        bool started = false;
        bool removes = false;
        bool removeOnDrop = false;
        bool returning = false;
    };

    struct DropTarget {
        ui::DropOffer *offer = nullptr;
        // Bumped for every new offer, so late reads of an old one are dropped.
        uint64_t generation = 0;
        bool application = false;
        bool desktop = false;
        bool window = false;
        bool files = false;
        // The dropped files and, once typed on the file thread, their types.
        std::vector<std::string> paths;
        std::optional<std::vector<std::string>> fileTypes;
        base::Job typesJob;
        std::optional<std::string> appId;
        std::optional<std::string> desktopId;
        ui::PointF position;
        bool inArea = false;
        bool entered = false;
        bool anchored = false;
        double referenceOrigin = 0;
        double lastAlong = 0;
        bool hasLastAlong = false;
        bool sourcePinned = false;
        int slot = -1;
        bool pinned = false;
        // A slot held open for an icon the dock does not have yet.
        bool gap = false;
        DockIcon *hovered = nullptr;
    };

    void rebuild();
    void retranslate();
    void updateMetrics(bool animate);
    void resize(bool animate);
    void layoutItems();
    void updateLayoutStart();
    void updatePanelRects(const base::Rect &rest, const base::Rect &drawn);
    base::Region inputBand(bool tracking) const;
    void updateInputRegion();
    void updateStrut();
    void updateGlass();
    void updateMinimizeTargets();
    void handOffLanded();
    double pixelRatio() const;
    int windowThickness() const;
    int restWindowThickness() const;
    int windowIconSize() const;
    int magnifiedIconSize() const;
    int magnifiedReach() const;
    DockMagnification magnification(double slotsStart, double slotsEnd) const;
    void pointerMoved(const ui::PointerEvent &event);
    void pointerLeft();
    void resumeMagnification(bool tracking);
    void magnificationMoved();
    bool stepMagnification(double nowMs);
    void prepareMagnification();
    double farSide(DockIcon *icon) const;
    base::Rect minimizedSlot(int index, int count) const;
    base::Rect applicationSlot(double position) const;
    double place(double position, double rest) const;
    void showTooltip(DockIcon *icon);
    ui::Bitmap iconImage(DockIcon *icon, double *scale) const;
    base::PointF iconCentre(DockIcon *icon) const;
    double distanceOffDock(const base::PointF &position) const;
    void onDragStarted();
    void onDragMoved(const base::PointF &location);
    void onDragEnded();
    void finishIconDrag();
    void dropEnter(ui::DropOffer *offer, const ui::PointF &position);
    uint32_t dropMove(const ui::PointF &position);
    void dropLeave();
    uint32_t dropAt(const ui::PointF &position);
    void resetDropTarget();
    void setDropHovered(DockIcon *icon);
    // The hovered application icon, when its application opens the dropped files.
    DockIcon *fileDropApplication() const;
    bool inApplicationArea(const ui::PointF &position) const;
    void enterApplicationArea();
    void anchorApplicationArea();
    void updateDropSlot();
    void updateDropGap();
    void announceGeometry();
    DockIcon *iconAt(const ui::PointF &position) const;
    void reorderMinimized();
    void retire(DockIcon *icon);
    void prepareDockMenu(const std::string &id, const std::shared_ptr<compositor::MenuModel> &dockMenu);
    static bool addDockMenuEntries(ui::Menu &menu, const std::shared_ptr<compositor::MenuModel> &model, int32_t id);
    void popupMenu(DockIcon *icon, std::unique_ptr<ui::Menu> menu);

    Shell *m_shell;
    std::unique_ptr<Dock> m_dock;
    std::unique_ptr<ShellWindow> m_window;
    Panel *m_panel = nullptr;
    std::unique_ptr<DockPresentation> m_applications;
    std::unique_ptr<DockPresentation> m_minimized;
    std::map<std::string, DockIcon *> m_applicationIcons;
    std::map<uint64_t, DockIcon *> m_minimizedIcons;
    std::map<uint64_t, MinimizedWindows::Entry> m_minimizedStates;
    std::set<DockIcon *> m_leavingIcons;
    DockIcon *m_trash = nullptr;
    DockMetrics m_metrics;
    DrawnDockMetrics m_fromMetrics;
    DrawnDockMetrics m_drawn;
    base::Rect m_windowRect;
    base::Rect m_restPanelRect;
    base::Rect m_drawnPanelRect;
    base::Region m_inputRegion;
    DockMagnifier m_magnifier;
    std::unique_ptr<MagnificationFrames> m_magnificationFrames;
    bool m_glassDark = false;
    bool m_glassOpaque = false;
    base::RectF m_divider;
    base::RectF m_trailingDivider;
    double m_trailingOpacity = 0;
    // Icons of applications and windows gone, until the next loop turn.
    std::vector<std::unique_ptr<ui::View>> m_retired;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    double m_surfaceLength = 0;
    double m_start = 0;
    double m_length = 0;
    double m_minimizedStart = 0;
    double m_restingLength = -1;
    double m_restingSurface = -1;
    double m_fromLength = 0;
    double m_bias = 0;
    double m_fromBias = 0;
    double m_toBias = 0;
    double m_fromEnds = -1;

    std::optional<IconDrag> m_drag;
    // A returned icon is in its slot from the next frame on; the compositor's
    // copy goes once that frame is out.
    bool m_releaseReturn = false;
    DropTarget m_drop;
    base::Rect m_announcedGeometry;
    base::Timer m_raiseTimer;
    base::Timer m_reorderTimer;

    std::unique_ptr<DockTooltip> m_tooltip;
    DockIcon *m_tooltipOwner = nullptr;
    base::Timer m_tooltipHide;
    std::unique_ptr<ui::Menu> m_menu;
    DockIcon *m_menuIcon = nullptr;
    bool m_menuOpen = false;
    base::Timer m_dockMenuWait;
    base::Subscription m_dockMenuUpdate;
    bool m_dockMenuPrepared = false;

    std::vector<uint64_t> m_landed;
    std::map<uint64_t, base::Rect> m_sentTargets;
    base::Timer m_strutSettle;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_DOCK_DOCK_WINDOW_H_
