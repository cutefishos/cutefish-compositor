// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modelled on chromium/src/ash/wm/window_state.{h,cc}, base_state.* and
// default_state.* (revision 7acd40ddefff49c9d36144999aafdbb674dbd07a).

#ifndef CUTEFISH_WM_WINDOW_STATE_H_
#define CUTEFISH_WM_WINDOW_STATE_H_

#include <optional>
#include <vector>

#include "base/geometry.h"
#include "wm/types.h"

namespace cutefish::scene {
class WindowAnimation;
}

namespace cutefish::wm {

class Window;
class WMEvent;

class WindowState
{
public:
    explicit WindowState(Window* window);
    WindowState(const WindowState&) = delete;
    WindowState& operator=(const WindowState&) = delete;
    ~WindowState();

    Window* window() const { return m_window; }

    WindowStateType stateType() const { return m_type; }

    bool isNormal() const { return m_type == WindowStateType::Normal; }
    bool isMaximized() const { return m_type == WindowStateType::Maximized; }
    bool isFullscreen() const { return m_type == WindowStateType::Fullscreen; }
    bool isMinimized() const { return m_type == WindowStateType::Minimized; }

    bool isSnapped() const { return isSnappedStateType(m_type); }

    bool canSnap() const;

    void onEvent(const WMEvent& event);

    bool hasRestoreBounds() const { return m_restoreBounds.has_value(); }
    base::Rect restoreBounds() const { return m_restoreBounds.value_or(base::Rect()); }
    void setRestoreBounds(const base::Rect& bounds) { m_restoreBounds = bounds; }
    void clearRestoreBounds() { m_restoreBounds.reset(); }
    void saveCurrentBoundsForRestore();

    // Normal when there is no history.
    WindowStateType restoreStateType() const;

    void updateMinimizeTarget();

private:
    void requestMinimizedSnapshot(scene::WindowAnimation* animation);
    void enterState(WindowStateType next);
    void updateBoundsFromState(WindowStateType previous);
    void updateVisibility(WindowStateType previous);
    void updateRestoreHistory(WindowStateType previous);
    void setBoundsDirect(const base::Rect& bounds);

    Window* m_window = nullptr;
    WindowStateType m_type = WindowStateType::Normal;
    std::optional<base::Rect> m_restoreBounds;

    // Bottom to top.
    std::vector<WindowStateType> m_restoreHistory;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_STATE_H_
