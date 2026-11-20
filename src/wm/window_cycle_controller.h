// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/window_cycle/window_cycle_controller.h
//           window_cycle_list.h and window_cycle_event_filter.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65
//
// Adapted for Cutefish: one entry per application, not per window.

#ifndef CUTEFISH_WM_WINDOW_CYCLE_CONTROLLER_H_
#define CUTEFISH_WM_WINDOW_CYCLE_CONTROLLER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>


#include "base/geometry.h"
#include "base/signal.h"
#include "base/timer.h"

namespace cutefish::wm {

class MruWindowTracker;
class Window;
class WindowManager;

class WindowCycleView
{
public:
    virtual ~WindowCycleView() = default;

    virtual void showWindowCycle(const std::vector<Window *> &entries, int selected,
                                 const std::string &output) = 0;
    virtual void selectWindowCycleEntry(int index) = 0;
    virtual void hideWindowCycle() = 0;

    // `location` is in global logical coordinates.
    virtual bool windowCycleViewContains(const base::PointF &location) const = 0;
    virtual int windowCycleEntryAt(const base::PointF &location) const = 0;
};

class WindowCycleController
{
public:
    enum class Direction {
        Forward,
        Backward,
    };

    static constexpr int kShowDelayMs = 150;
    static constexpr double kMouseMovementThreshold = 5;

    WindowCycleController(WindowManager *manager, MruWindowTracker *mruTracker);
    WindowCycleController(const WindowCycleController &) = delete;
    WindowCycleController &operator=(const WindowCycleController &) = delete;
    ~WindowCycleController();

    bool canCycle() const;

    void handleCycleWindow(Direction direction);

    bool isCycling() const { return m_cycling; }

    void completeCycling();

    void cancelCycling();

    void setView(WindowCycleView *view);

    std::vector<Window *> entries() const;
    int selectedIndex() const { return m_currentIndex; }
    bool isViewShown() const { return m_viewShown; }

    void showViewNowForTesting();

    // Returns true when the event was consumed.
    bool onKey(uint32_t keysym, bool pressed);

    void onPointerMotion(const base::PointF &location);
    bool onPointerButtonPressed(const base::PointF &location);

private:
    void startCycling();
    void step(Direction direction, bool startingCycle);
    void stopCycling(bool accept);
    void switchToApplication(const std::vector<Window *> &windows);
    void showView();
    void hideView();
    std::string outputForView() const;
    void onWindowRemoved(Window *window);

    WindowManager *m_manager = nullptr;
    MruWindowTracker *m_mruTracker = nullptr;
    WindowCycleView *m_view = nullptr;
    base::Subscription m_windowRemoved;
    base::Timer m_showTimer;
    bool m_cycling = false;
    std::vector<std::vector<Window *>> m_applications;
    int m_currentIndex = 0;
    bool m_viewShown = false;
    std::optional<base::PointF> m_pointerOrigin;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_CYCLE_CONTROLLER_H_
