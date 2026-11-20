// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_WINDOW_H_
#define CUTEFISH_WM_WINDOW_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>


#include "base/geometry.h"
#include "compositor/surface_observer.h"
#include "ui/bitmap.h"
#include "ui/path.h"
#include "wm/types.h"
#include "wm/window_frame.h"

namespace cutefish::compositor {
class Surface;
}

namespace cutefish::scene {
class Node;
}

namespace cutefish::wm {

class WindowManager;
class WindowObserver;
class WindowState;
class WindowVisualController;

// bounds() includes any server-side frame; contentBounds() is the client area.
class Window : public compositor::SurfaceObserver
{
public:
    Window(WindowManager *manager, compositor::Surface *rootSurface, const std::string &title = {},
           WindowType type = WindowType::Normal);
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    ~Window() override;

    WindowType type() const { return m_type; }

    bool isPopup() const { return m_type == WindowType::Popup; }

    uint64_t id() const { return m_id; }

    WindowManager *manager() const { return m_manager; }

    // Never a subsurface.
    compositor::Surface *rootSurface() const { return m_rootSurface; }

    // Null without a scene.
    scene::Node *node() const { return m_node; }

    scene::Node *visualNode() const;

    scene::Node *surfaceNode() const;

    const std::string &title() const { return m_title; }

    void setTitle(const std::string &title);

    const std::string &applicationId() const { return m_applicationId; }

    void setApplicationId(const std::string &id);

    bool skipTaskbar() const;

    // 0 when unknown. Never a privilege check.
    int64_t processId() const { return m_processId; }

    void setProcessId(int64_t pid) { m_processId = pid; }

    WindowRole role() const { return m_role; }

    void setRole(WindowRole role);

    bool isShellWindow() const { return m_role != WindowRole::Normal; }

    FocusPolicy focusPolicy() const { return m_focusPolicy; }

    void setFocusPolicy(FocusPolicy policy) { m_focusPolicy = policy; }

    PanelBehavior panelBehavior() const { return m_panelBehavior; }

    void setPanelBehavior(PanelBehavior behavior);

    // In output coordinates.
    void setClientPosition(const base::Point &position);

    // In output coordinates; null when the shell has not reported it.
    base::Rect minimizedGeometry() const;
    void setMinimizedGeometry(const base::Rect &geometry);
    void setMinimizedGeometry(Window *panel, const base::Rect &geometry);

    // Null while the window is not minimized.
    const ui::Bitmap &minimizedSnapshot() const { return m_minimizedSnapshot; }

    void setMinimizedSnapshot(const ui::Bitmap &snapshot);

    void beginMinimizeHandoff();
    void endMinimizeHandoff();

    static constexpr int kMinimizeHandoffTimeoutMs = 250;

    WindowState *state() { return m_state.get(); }

    const WindowState *state() const { return m_state.get(); }

    WindowStateType stateType() const;

    // Differs from stateType() only while a drag pulls a tiled window out of its state.
    WindowStateType presentedStateType() const;

    bool isRestoringFromDrag() const { return m_restoringFromDrag; }

    base::Rect bounds() const { return m_bounds; }

    base::Rect contentBounds() const { return frameInsets().inset(m_bounds); }

    // Null while fullscreen.
    Insets frameInsets() const;

    // Bypasses the state machine; user and client requests go through SetBoundsWMEvent.
    void setBounds(const base::Rect &bounds);

    // Offset from contentBounds().topLeft() to the root surface's origin.
    base::Point surfaceOrigin() const { return m_surfaceOrigin; }

    void setSurfaceOrigin(const base::Point &origin);

    // What the window has on screen now, in node() coordinates; bounds() is only a request.
    base::Rect presentedBounds() const;

    // In client-area size; an empty maximum size means unbounded.
    base::Size minimumSize() const { return m_minimumSize; }

    void setMinimumSize(const base::Size &size);

    base::Size maximumSize() const { return m_maximumSize; }

    void setMaximumSize(const base::Size &size);

    base::Size minimumOuterSize() const;
    base::Size maximumOuterSize() const;

    bool isResizable() const { return !isPopup() && m_resizable; }

    void setResizable(bool resizable) { m_resizable = resizable; }

    // In output coordinates. Input follows it; bounds() does not.
    base::Point presentationOffset() const { return m_presentationOffset; }
    void setPresentationOffset(const base::Point &offset);

    bool isPresentationHidden() const { return m_presentationHidden; }
    void setPresentationHidden(bool hidden);

    // What the client's buffer occupies outside its xdg_surface window geometry.
    Insets clientExteriorInsets() const { return m_clientExteriorInsets; }

    void setClientExteriorInsets(const Insets &insets);

    bool hasClientExteriorDecoration() const
    {
        return m_clientExteriorInsets.left > 0 && m_clientExteriorInsets.top > 0;
    }

    // How far the committed input region stays inside the surface on each edge.
    Insets clientInputInsets() const { return m_clientInputInsets; }

    void setClientInputInsets(const Insets &insets);

    bool hasClientInputMargin() const
    {
        return m_clientInputInsets.left > 0 && m_clientInputInsets.top > 0
                && m_clientInputInsets.right > 0 && m_clientInputInsets.bottom > 0;
    }

    void setDecorated(bool decorated);

    bool isDecorated() const { return m_decorated; }

    // Null unless decorated and a frame provider is installed.
    WindowFrame *frame() const { return m_frame.get(); }

    // False while minimized, hidden or on an inactive desk.
    bool isVisible() const { return m_visible && !m_deskHidden; }

    // The window's own visibility, whatever its desk.
    bool targetVisibility() const { return m_visible; }

    void setVisible(bool visible);

    bool isDeskHidden() const { return m_deskHidden; }

    void setDeskHidden(bool hidden);

    bool isHidden() const { return m_hidden; }

    void setHidden(bool hidden);

    // Multiplied by the client's own set_opacity.
    void setOpacity(float opacity);

    // `anchor` is in bounds() coordinates.
    void setPresentationScale(double scale, const base::PointF &anchor, bool animate);

    bool isActive() const;
    void activate();

    bool isResizing() const { return m_resizing; }

    // Also true for a move, which the xdg_toplevel resizing state does not cover.
    bool isDragged() const { return m_dragged; }

    bool isDragPreview() const { return m_dragPreview; }

    void setDragPreview(bool preview) { m_dragPreview = preview; }

    void maximize();
    void snapPrimary();
    void snapSecondary();
    void minimize();
    void restore();
    void setFullscreen(bool fullscreen);
    void requestBounds(const base::Rect &bounds);

    using CloseCallback = std::function<void()>;
    void setCloseCallback(CloseCallback callback);

    bool canClose() const { return static_cast<bool>(m_closeCallback); }

    // May destroy this window.
    void requestClose();

    using PingCallback = std::function<void()>;
    void setPingCallback(PingCallback callback);

    void ping();

    bool isResponsive() const { return m_responsive; }

    void setResponsive(bool responsive);

    using KillCallback = std::function<void()>;
    void setKillCallback(KillCallback callback);
    void kill();

    // Null unsets it. Returns false for a cycle or for this window itself.
    bool setTransientParent(Window *parent);

    Window *transientParent() const { return m_transientParent; }

    const std::vector<Window *> &transientChildren() const { return m_transientChildren; }

    bool isModal() const { return m_modal; }

    void setModal(bool modal);

    bool isProtectedFromControl() const { return m_protectedFromControl; }

    void setProtectedFromControl(bool protectedFromControl)
    {
        m_protectedFromControl = protectedFromControl;
    }

    // 0 means the whole bounds.
    int edgeReach() const { return m_edgeReach; }

    void setEdgeReach(int reach) { m_edgeReach = reach; }

    bool isAlwaysOnTop() const;

    void setAlwaysOnTop(bool alwaysOnTop);

    bool isAboveDock() const { return m_surfaceAboveDock; }

    // Deletes this window.
    void onSurfaceDestroying(compositor::Surface *surface) override;
    void onSurfaceCommit(compositor::Surface *surface) override;
    void onSurfaceWindowHintsChanged(compositor::Surface *surface) override;

    void addObserver(WindowObserver *observer);
    void removeObserver(WindowObserver *observer);
    bool hasObserver(const WindowObserver *observer) const;

private:
    friend class Activation;
    friend class TransientManager;
    friend class WindowState;
    friend class ToplevelEventHandler;

    void setActiveInternal(bool active);
    void setResizingInternal(bool resizing);
    void setDraggedInternal(bool dragged);
    void setRestoringFromDragInternal(bool restoring);
    void updateSurfacePlacement();
    void updateSurfaceTargetBounds();
    void updateNodeVisibility();
    void onEffectiveVisibilityChanged(bool wasVisible);
    void placeSheets();
    void updateFrame();
    // Hands the surface tree back to the compositor so it outlives this window's node.
    void detachSurfaceNode();

    const WindowType m_type;
    const uint64_t m_id;
    WindowManager *m_manager = nullptr;
    compositor::Surface *m_rootSurface = nullptr;
    std::string m_title;
    std::string m_applicationId;
    int64_t m_processId = 0;
    WindowRole m_role = WindowRole::Normal;
    FocusPolicy m_focusPolicy = FocusPolicy::Normal;
    PanelBehavior m_panelBehavior = PanelBehavior::StayAbove;
    base::Point m_clientPosition;
    base::Rect m_minimizedGeometry;
    uint64_t m_minimizedGeometryPanel = 0;
    ui::Bitmap m_minimizedSnapshot;
    uint64_t m_minimizeHandoff = 0;
    CloseCallback m_closeCallback;
    PingCallback m_pingCallback;
    KillCallback m_killCallback;
    std::unique_ptr<WindowState> m_state;

    scene::Node *m_node = nullptr;
    std::unique_ptr<WindowVisualController> m_visual;
    ui::Path m_bodyShape;
    bool m_castsShadow = true;
    std::unique_ptr<WindowFrame> m_frame;
    bool m_decorated = false;

    base::Rect m_bounds;
    base::Point m_surfaceOrigin;
    base::Rect m_surfaceTargetBounds;
    base::Point m_presentationOffset;
    bool m_presentationHidden = false;
    Insets m_clientExteriorInsets;
    Insets m_clientInputInsets;
    base::Size m_minimumSize {1, 1};
    base::Size m_maximumSize;
    bool m_resizable = true;
    bool m_visible = true;
    bool m_hidden = false;
    bool m_deskHidden = false;
    bool m_active = false;
    bool m_resizing = false;
    bool m_dragged = false;
    bool m_dragPreview = false;
    bool m_restoringFromDrag = false;
    bool m_modal = false;
    bool m_responsive = true;
    bool m_protectedFromControl = false;
    int m_edgeReach = 0;
    bool m_alwaysOnTop = false;
    bool m_surfaceAlwaysOnTop = false;
    bool m_surfaceAboveDock = false;
    float m_opacity = 1;
    float m_surfaceOpacity = 1;

    Window *m_transientParent = nullptr;
    std::vector<Window *> m_transientChildren;

    std::vector<WindowObserver *> m_observers;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_H_
