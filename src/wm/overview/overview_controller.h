// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_OVERVIEW_OVERVIEW_CONTROLLER_H_
#define CUTEFISH_WM_OVERVIEW_OVERVIEW_CONTROLLER_H_

#include <optional>
#include <set>
#include <vector>


#include "base/geometry.h"
#include "base/signal.h"
#include "input/event_sink.h"
#include "scene/overview_node.h"
#include "wm/activation.h"
#include "wm/desks/desks_controller.h"

namespace cutefish::wm {

class Desk;
class Window;
class WindowManager;

class OverviewController : public input::EventSink,
                           public ActivationObserver,
                           public DesksObserver
{
public:
    enum class State {
        Hidden,
        Entering,
        Shown,
        Exiting,
    };

    static constexpr int kWindowGap = 32;
    static constexpr int kMargin = 24;
    static constexpr double kDragThreshold = 8;

    static constexpr int kBarPadding = 12;
    static constexpr int kBarSpacing = 12;
    static constexpr int kLabelHeight = 24;
    static constexpr double kMinimumSideOnBar = 0.5;

    explicit OverviewController(WindowManager *manager);
    OverviewController(const OverviewController &) = delete;
    OverviewController &operator=(const OverviewController &) = delete;
    ~OverviewController() override;

    // Without them it never shows.
    void setNodes(scene::OverviewNode *node, scene::OverviewDeskBarNode *bar);
    scene::OverviewNode *node() const { return m_node; }
    scene::OverviewDeskBarNode *deskBar() const { return m_bar; }

    void setWallpaperNodes(std::vector<const scene::Node *> nodes);

    void setNext(input::EventSink *next) { m_next = next; }

    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Hidden; }
    bool capturesInput() const;
    bool isTransitioning() const
    {
        return m_state == State::Entering || m_state == State::Exiting;
    }
    // `timeMs` is on the frame clock.
    double shownProgress(double timeMs) const;
    void advance(double timeMs);

    void toggle();
    bool enter();
    // Activates `window` first when given.
    void exit(Window *window = nullptr);
    void exitImmediately();
    void retranslate();
    // Not while the fingers hold it.
    void completeTransition();

    // Deltas are in progress, 1 being the whole way in; times are steady-clock milliseconds.
    bool startSwipe(bool upward);
    void updateSwipe(double delta, double timeMs);
    void endSwipe(double timeMs);

    struct Hit {
        enum class Type {
            None,
            Item,
            Desk,
            DeskClose,
            NewDesk,
            Bar,
            Panel,
        };
        Type type = Type::None;
        int index = -1;
        int bar = -1;
    };
    Hit hitTest(const base::PointF &location) const;

    struct Item {
        Window *window = nullptr;
        base::RectF bounds;
    };
    const std::vector<Item> &items() const { return m_items; }
    const std::vector<scene::OverviewDeskBarNode::Bar> &bars() const { return m_bars; }
    int hovered() const { return m_hovered; }
    int selected() const { return m_selected; }

    bool onPointerMotion(input::Timestamp timestamp, const base::PointF &location) override;
    bool onPointerButton(input::Timestamp timestamp, const base::PointF &location, uint32_t evdevButton,
                         bool pressed) override;
    bool onPointerAxis(input::Timestamp timestamp, const base::PointF &location, const base::PointF &offset,
                       bool discrete) override;
    bool onPointerAxisStop(input::Timestamp timestamp, const base::PointF &location) override;
    bool onKey(input::Timestamp timestamp, uint32_t evdevCode, bool pressed) override;
    bool onSecureKey(input::Timestamp timestamp, uint32_t evdevCode, bool pressed) override;
    bool onTouchDown(input::Timestamp timestamp, int32_t id, const base::PointF &location) override;
    bool onTouchMotion(input::Timestamp timestamp, int32_t id, const base::PointF &location) override;
    bool onTouchUp(input::Timestamp timestamp, int32_t id) override;
    void onTouchCancel(input::Timestamp timestamp) override;

    void onWindowActivated(Window *gainedActive, Window *lostActive) override;

    void onDeskAdded(const Desk *desk) override;
    void onDeskRemoved(const Desk *desk) override;
    void onDeskReordered(int from, int to) override;
    void onDeskActivationChanged(const Desk *activated, const Desk *deactivated) override;
    void onDeskNameChanged(const Desk *desk) override;
    void onChosenDeskChanged() override;
    void onWindowDeskChanged(Window *window) override;

private:
    struct Press {
        Hit hit;
        base::PointF location;
        // None for the pointer.
        std::optional<int32_t> touch;
    };

    bool isOverviewWindow(const Window *window) const;
    void rebuild(bool animate, bool keepPlaces = false);
    void buildBars();
    void arrangeDraggedDesk();
    void dropDesk();
    base::RectF draggedBounds(const base::PointF &location) const;
    void updateNodes(bool animate);
    void updateChrome();
    std::string deskName(int index) const;
    bool canCloseDesks() const;

    void takePointer();
    void releasePointer();

    void landDeskSwitch();
    // exit() without landing a desk switch first.
    void leave(Window *window);
    void onOutputsChanged();
    bool acceptsPresses() const;
    void activateForExit(Window *window);
    void begin();
    void finish();
    void onNodeFinished(double target);

    void press(const Hit &hit, const base::PointF &location, std::optional<int32_t> touch);
    void move(const base::PointF &location);
    void release(const base::PointF &location);
    void cancelPress();
    void drop(const base::PointF &location);
    void activateItem(int index);
    void moveSelection(int dx, int dy);
    void setHover(const Hit &hit);

    WindowManager *m_manager = nullptr;
    scene::OverviewNode *m_node = nullptr;
    scene::OverviewDeskBarNode *m_bar = nullptr;
    std::vector<const scene::Node *> m_wallpapers;
    std::set<Window *> m_faded;
    input::EventSink *m_next = nullptr;
    State m_state = State::Hidden;
    std::vector<Item> m_items;
    std::vector<scene::OverviewDeskBarNode::Bar> m_bars;
    std::vector<std::vector<Desk *>> m_barDesks;

    int m_hovered = -1;
    Window *m_swipeTarget = nullptr;
    int m_selected = -1;
    Hit m_barHover;
    std::optional<Press> m_press;
    int m_dragged = -1;
    base::PointF m_dragOffset;
    struct DeskDrag {
        int bar = -1;
        Desk *desk = nullptr;
        double grab = 0;
        int target = -1;
    };
    std::optional<DeskDrag> m_deskDrag;
    base::PointF m_pointer;
    bool m_pointerTaken = false;
    bool m_cursorOverridden = false;
    bool m_activating = false;
    // Their releases are taken too.
    std::set<uint32_t> m_buttons;
    std::set<uint32_t> m_keys;
    std::set<int32_t> m_touches;
    base::PointF m_touchLocation;

    base::Subscription m_windowAdded;
    base::Subscription m_windowRemoved;
    base::Subscription m_outputsChanged;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_OVERVIEW_OVERVIEW_CONTROLLER_H_
