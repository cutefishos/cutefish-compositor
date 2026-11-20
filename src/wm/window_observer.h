// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_WINDOW_OBSERVER_H_
#define CUTEFISH_WM_WINDOW_OBSERVER_H_

#include "base/geometry.h"
#include "wm/types.h"

namespace cutefish::wm {

class Window;

class WindowObserver
{
public:
    virtual ~WindowObserver() = default;

    virtual void onWindowBoundsChanged(Window* window, const base::Rect& oldBounds) { }
    virtual void onWindowStateTypeChanged(Window* window, WindowStateType oldType) { }
    virtual void onWindowActivationChanged(Window* window, bool active) { }
    virtual void onWindowResizingChanged(Window* window, bool resizing) { }
    // Wider than onWindowResizingChanged, which is only the xdg_toplevel resizing state.
    virtual void onWindowDragStateChanged(Window* window, bool dragged) { }
    virtual void onWindowTransientParentChanged(Window* window, Window* oldParent) { }
    virtual void onWindowModalChanged(Window* window) { }
    virtual void onWindowResponsiveChanged(Window* window) { }

    virtual void onWindowApplicationIdChanged(Window* window) { }

    virtual void onWindowTitleChanged(Window* window) { }

    virtual void onWindowContentChanged(Window* window) { }

    // Not a change to the scene node's own visibility.
    virtual void onWindowVisibilityChanged(Window* window, bool visible) { }

    virtual void onWindowHiddenChanged(Window* window) { }
    virtual void onWindowDeskHiddenChanged(Window* window) { }

    virtual void onWindowMinimizedSnapshotChanged(Window* window) { }

    virtual void onWindowMinimizeLanded(Window* window) { }

    virtual void onWindowClientDecorationChanged(Window* window) { }

    virtual void onWindowFrameChanged(Window* window) { }

    // Called at the top of the window's destructor.
    virtual void onWindowDestroying(Window* window) { }
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_OBSERVER_H_
