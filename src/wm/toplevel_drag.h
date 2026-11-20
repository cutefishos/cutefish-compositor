// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modelled on chromium/src/components/exo/extended_drag_source.* (revision
// 7acd40ddefff49c9d36144999aafdbb674dbd07a), for xdg_toplevel_drag_v1 instead
// of zcr_extended_drag_v1.
//
// A drop on a window of its own process activates that window, which the client cannot do.

#ifndef CUTEFISH_WM_TOPLEVEL_DRAG_H_
#define CUTEFISH_WM_TOPLEVEL_DRAG_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "base/geometry.h"
#include "base/signal.h"
#include "compositor/surface_observer.h"
#include "input/drag.h"
#include "scene/window_animation.h"

namespace cutefish::clipboard {
class DataSource;
}

namespace cutefish::wm {

class Window;
class WindowManager;

class ToplevelDrag : public compositor::SurfaceObserver,
                     public input::DragWindowDelegate
{
public:
    ToplevelDrag(WindowManager *manager, std::shared_ptr<clipboard::DataSource> source);
    ~ToplevelDrag() override;

    bool hasToplevel() const { return m_surface != nullptr; }

    bool isDragging() const { return m_dragging; }

    // `offset` is in the window geometry of `surface`, a toplevel's root surface, mapped or not.
    void attach(compositor::Surface *surface, const base::Point &offset);

    void dragMoved(const base::PointF &location) override;
    void dragTargetChanged(compositor::Surface *target, bool accepted) override;
    void dragEnded(bool released) override;

    void onSurfaceDestroying(compositor::Surface *surface) override;
    void onSurfaceCommit(compositor::Surface *surface) override;

    static constexpr float kPreviewOpacity = 1.0f;
    static constexpr double kPreviewWidth = 320;
    static constexpr double kPreviewMaxScale = 0.5;
    static constexpr double kIconDistance = 40;
    static constexpr double kIconFadeDistance = 60;
    static constexpr int kSettleDelayMs = 500;

private:
    void onDragStarted();
    void onWindowAdded(Window *window);
    void onWindowRemoved(Window *window);
    Window *windowForSurface() const;
    void startMove(Window *window);
    void stopMove();
    void detach();
    void unwatchDockTarget();
    void updateCrossFade();

    WindowManager *m_manager = nullptr;
    std::shared_ptr<clipboard::DataSource> m_source;
    compositor::Surface *m_surface = nullptr;
    base::Point m_offset;
    bool m_dragging = false;
    base::PointF m_dragStart;
    base::PointF m_pointer;
    bool m_previewHidden = false;
    Window *m_moving = nullptr;
    // Zero unless the dragged window is still mapped after a drop.
    uint64_t m_dropWindow = 0;
    uint64_t m_preview = 0;
    int m_previewWidth = 0;
    // Zero when none.
    uint64_t m_docked = 0;
    compositor::Surface *m_dockTarget = nullptr;
    std::optional<scene::WindowAnimation::Style> m_animationStyle;
    base::Subscription m_dragStarted;
    base::Subscription m_windowAdded;
    base::Subscription m_windowRemoved;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_TOPLEVEL_DRAG_H_
