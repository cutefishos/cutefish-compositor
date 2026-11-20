// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/shell_surface_base.{h,cc}
//           chromium/src/components/exo/shell_surface.{h,cc}
//           chromium/src/components/exo/xdg_shell_surface.{h,cc}
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WM_SHELL_SURFACE_H_
#define CUTEFISH_WM_SHELL_SURFACE_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/timer.h"
#include "compositor/surface_observer.h"
#include "wm/types.h"
#include "wm/window_observer.h"

namespace cutefish::compositor {
class Surface;
}

namespace cutefish::wm {

class Window;
class WindowManager;

class ShellSurface : public compositor::SurfaceObserver, public WindowObserver
{
public:
    enum class Role {
        None,
        Toplevel,
        Popup,
    };

    // A client that never creates a decoration object decorates itself.
    enum class DecorationMode {
        ClientSide,
        ServerSide,
    };

    static constexpr DecorationMode kPreferredDecorationMode = DecorationMode::ServerSide;

    // `restoreStateType` is what a restore would land in.
    struct ConfigureState {
        base::Size size;
        WindowStateType stateType = WindowStateType::Normal;
        WindowStateType restoreStateType = WindowStateType::Normal;
        bool resizing = false;
        bool activated = false;
        // In client-area terms; 0x0 when unknown.
        base::Size bounds {0, 0};
        // Maximize is offered only to a resizable window.
        bool maximizable = true;
        bool suspended = false;
    };

    // Returns the serial to acknowledge, or 0 when no configure was sent.
    using ConfigureCallback = std::function<uint32_t(const ConfigureState &state)>;
    // Relative to the parent's window geometry.
    using PopupConfigureCallback = std::function<uint32_t(const base::Rect &bounds)>;
    // xdg_toplevel.close / xdg_popup.popup_done.
    using CloseCallback = std::function<void()>;
    using PingCallback = std::function<void()>;
    using KillCallback = std::function<void()>;

    ShellSurface(WindowManager *manager, compositor::Surface *surface);
    ShellSurface(const ShellSurface &) = delete;
    ShellSurface &operator=(const ShellSurface &) = delete;
    ~ShellSurface() override;

    compositor::Surface *rootSurface() const { return m_surface; }

    WindowManager *manager() const { return m_manager; }

    // Null until the surface has a role, acknowledged its initial configure and committed contents.
    Window *window() const { return m_window; }

    Role role() const { return m_role; }

    bool hasRole() const { return m_role != Role::None; }

    void setConfigureCallback(ConfigureCallback callback);
    void setPopupConfigureCallback(PopupConfigureCallback callback);
    void setCloseCallback(CloseCallback callback);
    void setPingCallback(PingCallback callback);
    void setKillCallback(KillCallback callback);

    // Returns false if a role has already been assigned.
    bool setToplevel();
    // `bounds` is relative to `parent`'s window geometry.
    bool setPopup(ShellSurface *parent, const base::Rect &bounds);

    // The xdg_surface stays and may be given a new role.
    void clearRole();

    ShellSurface *popupParent() const { return m_parentShell; }
    const std::vector<ShellSurface *> &childPopups() const { return m_childShells; }
    bool hasPopupDescendant(const compositor::Surface *root) const;

    Window *ownerWindow() const;

    // Pending until the next commit; an empty rect clears it.
    void setGeometry(const base::Rect &geometry);
    base::Rect visibleBounds() const;

    // False for an unknown or superseded serial; the caller posts the protocol error.
    bool acknowledgeConfigure(uint32_t serial);

    bool hasPendingConfigures() const { return !m_pendingConfigs.empty(); }

    size_t pendingConfigureCount() const { return m_pendingConfigs.size(); }

    // Without one the effective mode is ClientSide.
    bool hasDecorationObject() const { return m_hasDecorationObject; }

    void setHasDecorationObject(bool has);

    // The mode the client asked for, or nothing after unset_mode.
    void setRequestedDecorationMode(std::optional<DecorationMode> mode);

    DecorationMode decorationMode() const;

    // So the decoration configure can be sent before the toplevel configure.
    DecorationMode decorationModeFor(std::optional<DecorationMode> requested) const;

    bool hasDialogObject() const { return m_hasDialogObject; }

    void setHasDialogObject(bool has) { m_hasDialogObject = has; }

    // Kept across a remap; applied to the window whenever there is one.
    void setModal(bool modal);

    bool isModal() const { return m_modal; }

    void setTitle(const std::string &title);

    const std::string &title() const { return m_title; }

    void setApplicationId(const std::string &id);

    const std::string &applicationId() const { return m_applicationId; }

    void setProcessId(int64_t pid) { m_processId = pid; }

    int64_t processId() const { return m_processId; }

    // Pending until the next commit, like the geometry.
    void setMinimumSize(const base::Size &size);
    void setMaximumSize(const base::Size &size);

    // Returns false for a cycle. An unmapped parent counts as none, as xdg-shell requires.
    bool setTransientParent(ShellSurface *parent);

    ShellSurface *transientParent() const { return m_transientParentShell; }

    void maximize();
    void unmaximize();
    void minimize();
    void setFullscreen(bool fullscreen);
    void setFullscreenOutput(const std::string &waylandName);

    // The protocol layer checks the serial. `byTouch` fails without a touch point down on the window.
    bool startMove(bool byTouch = false);
    bool startResize(ResizeEdge edge, bool byTouch = false);

    void close();

    // `client` is only compared; null is the shell's own menus.
    void grab(const void *client = nullptr);

    const void *grabClient() const { return m_grabClient; }

    // Not owned; the caller clears it before the surface goes.
    void setPopupAnchorSurface(compositor::Surface *surface) { m_popupAnchorSurface = surface; }

    compositor::Surface *popupAnchorSurface() const { return m_popupAnchorSurface; }

    void configurePopup();
    void reposition(const base::Rect &bounds);

    base::Rect popupBounds() const { return m_popupBounds; }

    // In output coordinates.
    base::Point absoluteOrigin() const;

    void onSurfaceDestroying(compositor::Surface *surface) override;
    void onSurfaceCommit(compositor::Surface *surface) override;
    std::function<void()> onSurfaceCommitRequested(compositor::Surface *surface) override;

    void onWindowBoundsChanged(Window *window, const base::Rect &oldBounds) override;
    void onWindowStateTypeChanged(Window *window, WindowStateType oldType) override;
    void onWindowActivationChanged(Window *window, bool active) override;
    void onWindowResizingChanged(Window *window, bool resizing) override;
    void onWindowFrameChanged(Window *window) override;
    void onWindowHiddenChanged(Window *window) override;
    void onWindowDeskHiddenChanged(Window *window) override;
    void onWindowDestroying(Window *window) override;

private:
    // Coalesces the configures of a commit or state change into one.
    class ScopedConfigure
    {
    public:
        ScopedConfigure(ShellSurface *shellSurface, bool forceConfigure);
        ScopedConfigure(const ScopedConfigure &) = delete;
        ScopedConfigure &operator=(const ScopedConfigure &) = delete;
        ~ScopedConfigure();

        void setNeedsConfigure() { m_needsConfigure = true; }

    private:
        ShellSurface *m_shellSurface;
        // A configure callback can destroy the shell surface this lives inside.
        std::shared_ptr<ShellSurface *> m_alive;
        const bool m_forceConfigure;
        bool m_needsConfigure = false;
    };

    struct Config {
        uint32_t serial;
        base::Point originOffset;
        ResizeEdge resizeEdge;
        ConfigureState state;
        std::optional<base::Rect> popupBounds;
    };

    // Taken when the client commits and installed when that commit applies, which may be later.
    struct CommittedState {
        base::Rect geometry;
        base::Size minimumSize;
        base::Size maximumSize;
        base::Point originOffset;
        ResizeEdge resizeEdge = ResizeEdge::None;
        std::optional<ConfigureState> acknowledgedConfigure;
        bool decorationRemoval = false;
        std::optional<base::Rect> popupBounds;
    };

    void configure();
    void markBoundsContentReady();
    void awaitSizeChoice();
    void finishAwaitingContent();
    void stopAwaitingContent();
    void sendPopupConfigure(const base::Rect &bounds, bool reposition);
    void applyCommittedState(const CommittedState &state);
    void mapToplevel();
    void unmapToplevel();
    void mapPopup();
    void unmapPopup();
    void unmapChildPopups();
    void updatePopupPosition();
    void updateChildPopupPositions();
    void updateFromWindowGeometry();
    void updateSurfaceOrigin();
    void updateClientExteriorInsets();
    Insets clientExteriorInsets() const;
    Insets clientInputInsets() const;
    void updateWindowBoundsFromClient();
    void updateSizeConstraints();
    base::Size configureBounds() const;
    base::Rect placeNormalWindow(const base::Size &size) const;
    Insets normalFrameInsets() const;
    bool computeResizable() const;
    bool isPositionManaged() const;
    base::Size initialSizeForState(WindowStateType stateType) const;
    base::Point surfaceOriginForWindow() const;
    void resetConfigureState();
    void applyDecorationMode();
    void addChildShell(ShellSurface *child);
    void removeChildShell(ShellSurface *child);
    void linkTransientParent(ShellSurface *parent);
    // xdg-shell: children of an unmapped toplevel pass to its parent; a remap does not restore them.
    void releaseTransientChildren();
    void detachTransientRelationships();

    // Cleared in the destructor and re-checked after any callback, which may destroy this object.
    std::shared_ptr<ShellSurface *> m_self;

    WindowManager *m_manager = nullptr;
    compositor::Surface *m_surface = nullptr;
    Window *m_window = nullptr;
    Role m_role = Role::None;

    ConfigureCallback m_configureCallback;
    PopupConfigureCallback m_popupConfigureCallback;
    CloseCallback m_closeCallback;
    PingCallback m_pingCallback;
    KillCallback m_killCallback;

    std::string m_title;
    std::string m_applicationId;
    int64_t m_processId = 0;

    base::Rect m_geometry;
    base::Rect m_pendingGeometry;
    base::Size m_minimumSize;
    base::Size m_pendingMinimumSize;
    base::Size m_maximumSize;
    base::Size m_pendingMaximumSize;

    WindowStateType m_initialStateType = WindowStateType::Normal;
    base::Rect m_initialOutput;
    std::string m_fullscreenOutputName;
    bool m_initialConfigureSent = false;

    ScopedConfigure *m_scopedConfigure = nullptr;
    std::deque<Config> m_pendingConfigs;
    std::optional<ConfigureState> m_acknowledgedConfigure;
    std::optional<ConfigureState> m_boundsTransitionTarget;

    // Holds the surface where it was until the client catches up with an asynchronous resize.
    base::Point m_originOffset;
    base::Point m_pendingOriginOffset;
    base::Point m_pendingOriginOffsetAccumulator;

    ResizeEdge m_resizeEdge = ResizeEdge::None;
    ResizeEdge m_pendingResizeEdge = ResizeEdge::None;

    bool m_hasDecorationObject = false;
    bool m_decorationRemovalPending = false;
    std::optional<CommittedState> m_committed;
    std::optional<DecorationMode> m_requestedMode;
    bool m_hasDialogObject = false;
    bool m_modal = false;
    base::Rect m_configuredContentBounds;
    base::Size m_boundsTransitionOldContentSize;

    bool m_applyingClientBounds = false;

    bool m_clientChoosesNormalSize = false;

    std::vector<compositor::Surface *> m_awaitedContent;
    base::Timer m_contentAwaitTimer;

    ShellSurface *m_parentShell = nullptr;
    ShellSurface *m_transientParentShell = nullptr;
    std::vector<ShellSurface *> m_transientChildShells;
    std::vector<ShellSurface *> m_childShells;
    base::Rect m_popupBounds;
    std::optional<base::Rect> m_pendingPopupBounds;
    bool m_hasGrab = false;
    const void *m_grabClient = nullptr;
    compositor::Surface *m_popupAnchorSurface = nullptr;
    bool m_popupMapped = false;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_SHELL_SURFACE_H_
