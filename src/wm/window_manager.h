// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_WINDOW_MANAGER_H_
#define CUTEFISH_WM_WINDOW_MANAGER_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/geometry.h"
#include "base/lru_cache.h"
#include "base/signal.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "decoration/window_visual_style.h"
#include "display/display_types.h"
#include "input/event_sink.h"
#include "wm/activation.h"
#include "wm/placement.h"
#include "wm/stacking.h"
#include "wm/types.h"
#include "wm/window_frame.h"
#include "wm/window_modality_controller.h"
#include "wm/work_area.h"

namespace cutefish::compositor {
class Compositor;
class Surface;
} // namespace cutefish::compositor

namespace cutefish::input {
class Seat;
class SessionGate;
} // namespace cutefish::input

namespace cutefish::display {
class DisplayManager;
}

namespace cutefish::scene {
class Node;
}

namespace cutefish::wm {

class DesksController;
class MruWindowTracker;
class OverviewController;
class ShowDesktopController;
class ShellSurface;
class StatusItems;
class ToplevelEventHandler;
class TransientManager;
class Window;

struct ShellWindowMetadata {
    WindowRole role = WindowRole::Normal;
    FocusPolicy focusPolicy = FocusPolicy::Normal;
    PanelBehavior panelBehavior = PanelBehavior::StayAbove;
    base::Point position;
    bool hasPosition = false;
    std::string outputName;
    // The window a Sheet hangs from, by Window::id; zero for none.
    uint64_t parentWindow = 0;
    bool protectedFromControl = false;
    int edgeReach = 0;
};

// The app id and pid must be set here: the task list reads them only from windowAdded.
struct WindowInit {
    WindowType type = WindowType::Normal;
    base::Point origin;
    base::Size requestedSize;
    std::string title;
    std::string applicationId;
    int64_t processId = 0;
    bool decorated = false;
    base::Size minimumSize {1, 1};
    base::Size maximumSize;
    FocusPolicy focusPolicy = FocusPolicy::Normal;
    Window *transientParent = nullptr;
    // Cascades from the application's most recent window instead of centering.
    bool positionManaged = false;
    bool modal = false;
    // Empty means outputForNewWindow().
    base::Rect output;
    std::function<void()> closeCallback;
};

// Sees pointer and key events before the seat dispatches them to clients.
class WindowManager : public input::EventSink, public ActivationObserver
{
public:
    // `seat` may be null.
    WindowManager(compositor::Compositor *compositor, input::Seat *seat);
    WindowManager(const WindowManager &) = delete;
    WindowManager &operator=(const WindowManager &) = delete;
    ~WindowManager() override;

    compositor::Compositor *compositor() const { return m_compositor; }

    base::WeakPtr<WindowManager> weakPtr() { return m_weakFactory.getWeakPtr(); }
    // `task` does not run if the window manager is destroyed first.
    void runLater(std::chrono::milliseconds delay, std::function<void()> task);

    input::Seat *seat() const { return m_seat; }

    // Null leaves windows undecorated. Set before any window exists.
    FrameProvider *frameProvider() const { return m_frameProvider; }

    void setFrameProvider(FrameProvider *provider) { m_frameProvider = provider; }

    const decoration::WindowVisualStyle &visualStyle() const { return m_visualStyle; }

    void setVisualStyle(const decoration::WindowVisualStyle &style);

    // Null leaves the window manager unrestricted.
    void setSessionGate(input::SessionGate *gate) { m_sessionGate = gate; }

    input::SessionGate *sessionGate() const { return m_sessionGate; }

    bool allowsActivation() const;
    scene::Node *inputRoot() const;

    Activation *activation() { return &m_activation; }

    // Keyboard focus without activation, until the window is hidden or destroyed.
    void setKeyboardGrab(Window *window);
    Window *keyboardGrab() const { return m_keyboardGrab; }

    // Ctrl+Shift+V to a terminal, Ctrl+V elsewhere, after held keys are released.
    void sendPaste();
    static constexpr int kPasteWaitMs = 2000;

    TransientManager *transientManager() { return m_transientManager.get(); }

    MruWindowTracker *mruWindowTracker() const { return m_mruTracker.get(); }

    DesksController *desks() const { return m_desks.get(); }

    OverviewController *overview() const { return m_overview.get(); }

    ShowDesktopController *showDesktop() const { return m_showDesktop.get(); }

    StatusItems *statusItems() const { return m_statusItems.get(); }

    ToplevelEventHandler *eventHandler() { return m_eventHandler.get(); }

    // Sizes are of the client area; an empty `init.requestedSize` uses the surface's.
    Window *createWindow(compositor::Surface *rootSurface, const WindowInit &init = {});
    void destroyWindow(Window *window);

    // Bottom to top.
    const std::vector<Window *> &windows() const { return m_stackingOrder; }

    // Never lifts a window past one of a higher layer.
    void raise(Window *window);

    void stackAtBottomOfLayer(Window *window);

    void restackScene();

    Window *windowForSurface(compositor::Surface *surface) const;

    void quitApplication(Window *window);

    bool belongsToApplication(const Window *candidate, const Window *window) const;

    // Returns empty for an ungrouped window. Unset, the app id or else the pid groups.
    void setApplicationResolver(std::function<std::string(const Window *)> resolver)
    {
        m_applicationResolver = std::move(resolver);
    }

    void hideApplication(Window *window);

    void unhideApplication(Window *window);

    // Null hides every application.
    void hideOtherApplications(Window *window);

    void setTerminateProcess(std::function<void(int64_t pid)> terminate)
    {
        m_terminateProcess = std::move(terminate);
    }

    void setQuitGracePeriod(std::chrono::milliseconds period) { m_quitGracePeriod = period; }

    // Returns whether the application takes a quit request as a whole.
    void setApplicationQuit(std::function<bool(Window *)> quit)
    {
        m_applicationQuit = std::move(quit);
    }

    void forceQuit(Window *window);

    void setForceQuitProcess(std::function<void(int64_t pid)> forceQuit)
    {
        m_forceQuitProcess = std::move(forceQuit);
    }

    // Returns whether force quit was offered.
    void setNotRespondingPrompt(std::function<bool(Window *)> prompt)
    {
        m_notRespondingPrompt = std::move(prompt);
    }

    bool promptNotResponding(Window *window);

    // Identifiers are never reused.
    Window *windowById(uint64_t id) const;

    // Metadata whose role is Normal takes the surface out of the shell model.
    void setShellMetadata(compositor::Surface *surface, const ShellWindowMetadata &metadata);
    void clearShellMetadata(compositor::Surface *surface);
    const ShellWindowMetadata *shellMetadata(compositor::Surface *surface) const;

    void onWindowStackingChanged(Window *window);

    void placeSheet(Window *sheet);

    void onWindowModalChanged(Window *window);
    void onWindowModalityInputsChanged(Window *window);
    void onWindowVisibilityChanged(Window *window);
    // Bounds are in global coordinates.
    void onWindowTargetBoundsChanged(Window *window, const base::Rect &oldBounds,
                                     const base::Rect &newBounds);

    void setExclusiveZone(const void *token, ScreenEdge edge, int thickness,
                          const std::string &outputName = {}, bool yieldsToMaximized = false);
    void clearExclusiveZone(const void *token);
    bool hasMaximizedWindow(const base::Rect &outputBounds) const;
    void setDockHidden(const Window *dock, bool hidden);
    void updateWorkAreas();

    void setSurfaceOwner(compositor::Surface *surface, Window *owner);
    void clearSurfaceOwner(compositor::Surface *surface);

    void setAuxiliarySurface(compositor::Surface *surface, Window *owner);

    // Null clears it. The node is not owned by the window manager.
    void setDragOverlay(scene::Node *overlay, Window *below);

    scene::Node *dragOverlay() const { return m_dragOverlay; }

    void setDeskOverlays(std::vector<scene::Node *> overlays);

    void setDockTransitionsAbovePanels(bool above);

    void pushPopupGrab(ShellSurface *popup);
    void removePopupGrab(ShellSurface *popup);

    const std::vector<ShellSurface *> &popupGrabs() const { return m_popupGrabs; }

    void dismissPopupGrabs();

    void cancelInteractions();

    // `location` is in output coordinates.
    Window *windowAt(const base::PointF &location, compositor::Surface **hitSurface = nullptr,
                     base::PointF *surfaceLocation = nullptr) const;

    Window *capturableWindowAt(const base::PointF &location) const;

    Window *frameAt(const base::PointF &location, FrameArea *area = nullptr) const;

    Window *resizeHandleAt(const base::PointF &location, bool touch, ResizeEdge *edge) const;

    // Callbacks are re-entrant, so a held pointer must be re-checked before use.
    bool hasWindow(const Window *window) const;

    base::Rect outputBounds() const;
    base::Rect outputBoundsFor(const Window *window) const;
    void setWindowOutput(Window *window, const std::string &waylandName);
    void clearWindowOutput(Window *window);
    void setOutputBounds(const base::Rect &bounds);
    void setDisplayManager(display::DisplayManager *manager);

    display::DisplayManager *displayManager() const { return m_displayManager; }

    base::Rect workAreaFor(const Window *window) const;
    base::Rect workAreaFor(const Window *window, const base::Rect &bounds) const;
    base::Rect visibleAreaFor(const Window *window) const;
    base::Rect visibleAreaFor(const Window *window, const base::Rect &bounds) const;
    void setWorkArea(const base::Rect &bounds);

    base::Rect outputForNewWindow() const;
    // Empty when there is no such output.
    base::Rect workAreaForOutput(const base::Rect &outputBounds) const;
    base::Rect restingWorkAreaForOutput(const base::Rect &outputBounds) const;
    base::Rect placementWorkAreaForOutput(const base::Rect &outputBounds) const;
    base::Rect boundsForNewWindow(const Window *window, base::Rect bounds) const;
    // `position` is in global coordinates.
    void moveWindowTo(Window *window, const base::Point &position);

    struct OutputArea {
        base::Rect bounds;
        base::Rect workArea;
        double scale = 1;
        // The wl_output name.
        std::string name;
        bool primary = false;

        bool operator==(const OutputArea &) const = default;
    };
    std::vector<OutputArea> outputAreas() const;

    bool onPointerMotion(input::Timestamp timestamp, const base::PointF &location) override;
    bool onPointerButton(input::Timestamp timestamp, const base::PointF &location, uint32_t evdevButton,
                         bool pressed) override;
    bool onPointerAxis(input::Timestamp timestamp, const base::PointF &location, const base::PointF &offset,
                       bool discrete) override;
    bool onPointerAxisStop(input::Timestamp timestamp, const base::PointF &location) override;
    bool onKey(input::Timestamp timestamp, uint32_t evdevCode, bool pressed) override;
    bool onTouchDown(input::Timestamp timestamp, int32_t id, const base::PointF &location) override;
    bool onTouchMotion(input::Timestamp timestamp, int32_t id, const base::PointF &location) override;
    bool onTouchUp(input::Timestamp timestamp, int32_t id) override;
    void onTouchCancel(input::Timestamp timestamp) override;

    void onWindowActivated(Window *gainedActive, Window *lostActive) override;

    base::Signal<Window *> windowAdded;
    base::Signal<Window *> windowRemoved;
    base::Signal<Window *> activeWindowChanged;
    // Every pointer motion, before any drag, grab or frame takes it.
    base::Signal<const base::PointF &> pointerMoved;
    base::Signal<> pointerTargetsChanged;
    base::Signal<> outputsChanged;
    base::Signal<Window *> windowMoved;
    base::Signal<> exclusiveZonesChanged;
    base::Signal<> popupGrabsChanged;
    // The reach into the window, then the slide duration in milliseconds.
    base::Signal<Window *, int, int> fullscreenRevealChanged;

private:
    bool hasApplicationWindow(int64_t process) const;

    bool isInputMethodPress(const base::PointF &location) const;

    void insertIntoStackingOrder(Window *window);
    void applyExclusiveZones();
    base::Rect zonedWorkArea(const base::Rect &workArea, const base::Rect &outputBounds,
                             const std::string &outputName, bool primary) const;
    void notifyWorkAreaChanged();
    bool requestedPlacement(const Window *window, base::Rect *bounds) const;
    void notifyOutputsChanged();
    // Runs before anything fits windows to the new layout.
    void followDisplays();
    bool isInsidePopupGrabChain(compositor::Surface *surface) const;
    Window *popupGrabRoot() const;
    bool isPopupGrabMenuBar(const Window *window) const;
    bool isPopupGrabAnchor(compositor::Surface *surface) const;
    bool pressReachesPopupGrabRoot(const Window *target) const;
    void updatePopupKeyboard();
    bool isCaptureWindow(const Window *window) const;
    Window *modalBlockedWindowAt(const base::PointF &location) const;
    bool handlePointerButton(input::Timestamp timestamp, const base::PointF &location, uint32_t code,
                             bool pressed);
    bool isPopupSurface(compositor::Surface *surface) const;
    bool isPopupAt(const base::PointF &location) const;
    Window *drawnWindowAt(const base::PointF &location) const;
    Window *windowTrackingFramePress() const;
    void setFrameHover(Window *window);
    void activateForPress(Window *window);
    void releaseKeyboardGrab();
    void attachSheet(Window *sheet, uint64_t parentId);
    void rememberPlacement(const Window *window);
    bool restorePlacement(const Window *window, base::Rect *bounds) const;

    compositor::Compositor *m_compositor = nullptr;
    input::Seat *m_seat = nullptr;
    input::SessionGate *m_sessionGate = nullptr;
    decoration::WindowVisualStyle m_visualStyle;
    FrameProvider *m_frameProvider = nullptr;
    Window *m_frameHover = nullptr;
    scene::Node *m_dragOverlay = nullptr;
    Window *m_dragOverlayBelow = nullptr;

    std::vector<std::unique_ptr<Window>> m_windows;
    // Bottom to top. Mirrors m_windows but is reordered by raise().
    std::vector<Window *> m_stackingOrder;

    Activation m_activation;
    Window *m_keyboardGrab = nullptr;
    Window *m_popupKeyboardRoot = nullptr;
    compositor::Surface *m_popupKeyboardSurface = nullptr;
    std::unique_ptr<base::Timer> m_pasteTimer;
    std::chrono::steady_clock::time_point m_pasteDeadline;
    std::unique_ptr<TransientManager> m_transientManager;
    std::unique_ptr<MruWindowTracker> m_mruTracker;
    std::unique_ptr<DesksController> m_desks;
    std::unique_ptr<OverviewController> m_overview;
    std::unique_ptr<ShowDesktopController> m_showDesktop;
    std::unique_ptr<StatusItems> m_statusItems;
    std::vector<scene::Node *> m_deskOverlays;
    bool m_dockTransitionsAbovePanels = true;
    std::unique_ptr<ToplevelEventHandler> m_eventHandler;
    Placement m_placement;
    WindowModalityController m_modality;
    display::DisplayManager *m_displayManager = nullptr;
    base::Subscription m_displaysChanged;
    std::vector<display::LogicalDisplay> m_previousDisplays;
    base::Rect m_outputBounds;
    base::Rect m_workArea;
    std::vector<OutputArea> m_outputAreas;
    ExclusiveZones m_exclusiveZones;
    std::vector<base::Rect> m_maximizedOutputs;
    std::set<const Window *> m_hiddenDocks;
    std::map<compositor::Surface *, ShellWindowMetadata> m_shellMetadata;
    std::map<const Window *, std::string> m_windowOutputs;
    struct RememberedPlacement {
        base::Rect bounds;
        base::Rect output;
    };
    static constexpr size_t kMaxRememberedPlacements = 256;
    mutable base::LruCache<std::string, RememberedPlacement> m_rememberedPlacements {
            kMaxRememberedPlacements};

    std::map<compositor::Surface *, Window *> m_surfaceOwners;
    std::vector<compositor::Surface *> m_auxiliarySurfaces;
    // Outermost first.
    std::vector<ShellSurface *> m_popupGrabs;
    // Their releases are consumed too, or a client would see a release without a press.
    std::set<uint32_t> m_consumedButtons;
    std::map<int32_t, Window *> m_frameTouches;
    std::function<void(int64_t pid)> m_terminateProcess;
    std::function<bool(Window *)> m_applicationQuit;
    std::function<std::string(const Window *)> m_applicationResolver;
    std::function<void(int64_t pid)> m_forceQuitProcess;
    std::function<bool(Window *)> m_notRespondingPrompt;
    std::chrono::milliseconds m_quitGracePeriod {3000};
    base::WeakPtrFactory<WindowManager> m_weakFactory {this};
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_MANAGER_H_
