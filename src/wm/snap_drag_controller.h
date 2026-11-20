// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modelled on the snap and maximize half of
// chromium/src/ash/wm/workspace/workspace_window_resizer.cc (revision
// 7acd40ddefff49c9d36144999aafdbb674dbd07a).

#ifndef CUTEFISH_WM_SNAP_DRAG_CONTROLLER_H_
#define CUTEFISH_WM_SNAP_DRAG_CONTROLLER_H_

#include <optional>

#include "base/geometry.h"
#include "base/timer.h"
#include "wm/window_snap.h"

namespace cutefish::scene {
class SnapPreviewNode;
}

namespace cutefish::wm {

class Window;
class WindowManager;

class SnapDragController
{
public:
    explicit SnapDragController(WindowManager *manager);
    SnapDragController(const SnapDragController &) = delete;
    SnapDragController &operator=(const SnapDragController &) = delete;
    ~SnapDragController();

    // `start` is the pointer position the move began at, in output coordinates.
    void begin(Window *window, const base::PointF &start);

    // Only call while the window is being moved: a resize never snaps.
    void update(const base::PointF &location);

    // Returns what the drag settled on, for the caller to apply.
    SnapType end();

    SnapType snapType() const { return m_snapType; }

    void triggerDwellForTesting() { fireDwell(); }

private:
    void startDwell();
    void stopDwell();
    void fireDwell();
    void setSnapType(SnapType type);
    void updatePreview();
    void clearPreview();
    SnapType constrained(SnapType type) const;
    bool isBelowTopEdge(const base::PointF &location) const;

    WindowManager *m_manager = nullptr;
    Window *m_window = nullptr;
    SnapType m_snapType = SnapType::None;
    bool m_canSnapToMaximize = false;
    std::optional<base::PointF> m_dwellLocation;
    bool m_dwellPending = false;
    base::Timer m_dwellTimer;
    scene::SnapPreviewNode *m_preview = nullptr;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_SNAP_DRAG_CONTROLLER_H_
