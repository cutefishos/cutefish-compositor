// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_SHOW_DESKTOP_CONTROLLER_H_
#define CUTEFISH_WM_SHOW_DESKTOP_CONTROLLER_H_

#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "wm/activation.h"
#include "wm/desks/desks_controller.h"
#include "wm/window_observer.h"

namespace cutefish::wm {

class Window;
class WindowManager;

class ShowDesktopController : public ActivationObserver,
                              public DesksObserver,
                              public WindowObserver
{
public:
    static constexpr double kSlideDurationMs = 250;
    // In logical pixels.
    static constexpr int kSliverWidth = 12;

    explicit ShowDesktopController(WindowManager *manager);
    ShowDesktopController(const ShowDesktopController &) = delete;
    ShowDesktopController &operator=(const ShowDesktopController &) = delete;
    ~ShowDesktopController() override;

    bool isActive() const { return m_active; }
    bool isMovedAside(const Window *window) const;
    // 0 with every window in place, 1 with all of them aside.
    double progress() const { return m_progress; }

    void toggle();
    void enter();
    // `activate`, when given, becomes active.
    void exit(Window *activate = nullptr);
    void exitImmediately();

    void advance(double frameTimeMs);

    void onWindowActivated(Window *gainedActive, Window *lostActive) override;

    void onDeskActivationChanged(const Desk *activated, const Desk *deactivated) override;
    void onDeskSwitchAnimationLaunching() override;

    void onWindowBoundsChanged(Window *window, const base::Rect &oldBounds) override;
    void onWindowVisibilityChanged(Window *window, bool visible) override;
    void onWindowDestroying(Window *window) override;

private:
    base::Point awayOffset(const Window *window) const;
    void slideTo(double target);
    void present();
    void release(Window *window);
    void releaseAll();
    void requestFrame();

    WindowManager *m_manager = nullptr;
    bool m_active = false;
    // Bottom to top.
    std::vector<Window *> m_windows;
    double m_progress = 0;
    double m_slideFrom = 0;
    double m_slideTo = 0;
    double m_slideStarted = -1;
    bool m_sliding = false;
    base::Subscription m_frameStarted;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_SHOW_DESKTOP_CONTROLLER_H_
