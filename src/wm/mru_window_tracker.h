// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/mru_window_tracker.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_WM_MRU_WINDOW_TRACKER_H_
#define CUTEFISH_WM_MRU_WINDOW_TRACKER_H_

#include <vector>

#include "base/signal.h"
#include "wm/activation.h"

namespace cutefish::wm {

class Window;
class WindowManager;

bool canIncludeWindowInMruList(const Window *window);

class MruWindowTracker : public ActivationObserver
{
public:
    using WindowList = std::vector<Window *>;

    explicit MruWindowTracker(WindowManager *manager);
    MruWindowTracker(const MruWindowTracker &) = delete;
    MruWindowTracker &operator=(const MruWindowTracker &) = delete;
    ~MruWindowTracker() override;

    // Most recently used first, minimized windows included.
    WindowList buildMruWindowList() const;

    WindowList buildWindowListForCycling() const;

    void onWindowActivated(Window *gainedActive, Window *lostActive) override;

    void onWindowRemoved(Window *window);

    void onWindowMovedOutFromRemovingDesk(Window *window);

private:
    void setActiveWindow(Window *window);

    WindowManager *m_manager = nullptr;
    base::Subscription m_windowRemoved;
    // Least recently used first, so the active window is at the back.
    WindowList m_mruWindows;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_MRU_WINDOW_TRACKER_H_
