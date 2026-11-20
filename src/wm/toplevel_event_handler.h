// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modelled on chromium/src/ash/wm/toplevel_window_event_handler.* and the
// drag-lifecycle half of ash/wm/workspace/workspace_window_resizer.*
// (revision 7acd40ddefff49c9d36144999aafdbb674dbd07a).

#ifndef CUTEFISH_WM_TOPLEVEL_EVENT_HANDLER_H_
#define CUTEFISH_WM_TOPLEVEL_EVENT_HANDLER_H_

#include <memory>
#include <optional>

#include "base/geometry.h"
#include "base/signal.h"
#include "wm/window_snap.h"

namespace cutefish::wm {

class SnapDragController;
class Window;
class WindowManager;
class WindowResizer;

class ToplevelEventHandler
{
public:
    enum class DragResult {
        Success,
        Revert,
        WindowDestroyed,
    };

    explicit ToplevelEventHandler(WindowManager *manager);
    ToplevelEventHandler(const ToplevelEventHandler &) = delete;
    ToplevelEventHandler &operator=(const ToplevelEventHandler &) = delete;
    ~ToplevelEventHandler();

    // `pointerPosition` is in output coordinates. With `touchId` the drag follows that touch point.
    bool beginMove(Window *window, const base::PointF &pointerPosition,
                   std::optional<int32_t> touchId = std::nullopt);
    bool beginResize(Window *window, ResizeEdge edge, const base::PointF &pointerPosition,
                     std::optional<int32_t> touchId = std::nullopt);

    bool isDragging() const { return m_resizer != nullptr; }

    bool isTouchDrag() const { return m_resizer && m_touchId; }

    Window *draggedWindow() const;
    // ResizeEdge::None while moving, or when no drag is running.
    ResizeEdge draggedEdge() const;

    SnapDragController *snapController() const { return m_snapController.get(); }

    // Each returns true if the drag consumed the event.
    bool onPointerMotion(const base::PointF &location);
    bool onPointerButtonReleased();
    bool onTouchMotion(int32_t id, const base::PointF &location);
    bool onTouchUp(int32_t id);
    void onTouchCancel();
    bool onKeyPressed(uint32_t evdevCode);

    void completeDrag(DragResult result);

    // Called by WindowManager just before `window` is destroyed.
    void onWindowRemoving(Window *window);

private:
    bool beginDrag(Window *window, ResizeEdge edge, const base::PointF &pointerPosition,
                   std::optional<int32_t> touchId);
    void drag(const base::PointF &location);
    // Called with the drag already detached from the handler.
    void settleDrag(WindowResizer *resizer, SnapType snapped, const base::Rect &initialRestoreBounds);
    void applyMotion(const base::PointF &location);
    void holdResize();
    void releaseHeldResize();

    WindowManager *m_manager = nullptr;
    // A resize takes one step per frame: the newest pointer move when the next frame starts.
    bool m_holdingResize = false;
    std::optional<base::PointF> m_heldResize;
    std::optional<base::Subscription> m_frameStarted;
    std::unique_ptr<WindowResizer> m_resizer;
    std::optional<int32_t> m_touchId;
    std::unique_ptr<SnapDragController> m_snapController;
    // Empty unless the drag started from a tiled state.
    base::Rect m_initialRestoreBounds;
    WindowStateType m_initialStateType = WindowStateType::Normal;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_TOPLEVEL_EVENT_HANDLER_H_
