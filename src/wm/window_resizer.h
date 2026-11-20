// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modelled on chromium/src/ash/wm/window_resizer.* and
// default_window_resizer.* (revision 7acd40ddefff49c9d36144999aafdbb674dbd07a).

#ifndef CUTEFISH_WM_WINDOW_RESIZER_H_
#define CUTEFISH_WM_WINDOW_RESIZER_H_

#include "base/geometry.h"
#include "wm/types.h"

namespace cutefish::wm {

class Window;

class WindowResizer
{
public:
    // ResizeEdge::None means a move. `restoreBounds` is set only when a move pulls a
    // tiled window out of its state; an empty `visibleArea` means `workArea`.
    WindowResizer(Window *window, const base::PointF &startLocation, ResizeEdge edge,
                  const base::Rect &workArea, const base::Rect &restoreBounds = base::Rect(),
                  const base::Rect &visibleArea = base::Rect());

    Window *window() const { return m_window; }

    ResizeEdge edge() const { return m_edge; }

    bool isResize() const { return m_edge != ResizeEdge::None; }

    base::Rect initialBounds() const { return m_initialBounds; }

    base::Rect restoreBounds() const { return m_restoreBounds; }

    // False until the drag has passed the threshold.
    bool hasMovedOrResized() const { return m_movedOrResized; }

    // Does not touch the window.
    base::Rect calculateBoundsForDrag(const base::PointF &location) const;

    void setAreas(const base::Rect &workArea, const base::Rect &visibleArea);

    void drag(const base::PointF &location);
    void completeDrag();
    void revertDrag();

private:
    int widthForDrag(int *deltaX) const;
    int heightForDrag(int *deltaY) const;

    Window *m_window = nullptr;
    ResizeEdge m_edge = ResizeEdge::None;
    base::PointF m_initialLocation;
    base::Rect m_initialBounds;
    base::Rect m_restoreBounds;
    base::Rect m_workArea;
    base::Rect m_visibleArea;
    bool m_movedOrResized = false;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_RESIZER_H_
