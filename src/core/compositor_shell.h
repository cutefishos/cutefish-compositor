// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_CORE_COMPOSITOR_SHELL_H_
#define CUTEFISH_CORE_COMPOSITOR_SHELL_H_

#include <memory>
#include <string>
#include <vector>

#include "auth/auth_ui.h"
#include "base/signal.h"
#include "base/timer.h"
#include "compositor/compositor.h"
#include "compositor/output.h"
#include "core/compositor_options.h"
#include "core/main_loop.h"
#include "cursor/cursor.h"
#include "cursor/output_cursor_router.h"
#include "cursor/preferences.h"
#include "decoration/decoration.h"
#include "decoration/decoration_theme.h"
#include "decoration/shadow_node.h"
#include "decoration/theme_preferences.h"
#include "display/display_manager.h"
#include "display/display_runtime.h"
#include "display/night_light.h"
#include "input/input_manager.h"
#include "input/keyboard_settings.h"
#include "input/seat.h"
#include "scene/scene.h"
#include "scene/screen_transition_node.h"
#include "session/applications_service.h"
#include "session/window_debug_service.h"
#include "session/power_button.h"
#include "session/screen_saver_service.h"
#include "session/secure_container.h"
#include "session/session_controller.h"
#include "session/session_teardown.h"
#include "session/sleep_lock.h"
#include "wm/dock_layout_manager.h"
#include "wm/dock_preferences.h"
#include "wm/shadow_controller.h"
#include "wm/window_manager.h"
#include "wm/wm_shadow_controller_delegate.h"

struct wl_global;

namespace cutefish {

namespace backend {
class DrmBackend;
class HeadlessBackend;
class LibinputBackend;
class LibseatSession;
} // namespace backend

namespace computer_control {
class Controller;
class PermissionConsent;
class RememberedApps;
} // namespace computer_control

namespace display {
class CallbackNativeDisplayDelegate;
class DisplayConfigurationController;
class DisplayTopologyCoordinator;

namespace ipc {
class DisplayConfigService;
}
} // namespace display

namespace wayland {
class ExecutableSecurityDelegate;
class IdleInhibition;
class DmaBufScanoutPolicy;
class LinuxDmabufGlobal;
class ProtocolGlobals;
class Server;
class ShellControlChannel;
} // namespace wayland

namespace accelerators {
class AcceleratorConfiguration;
class AcceleratorController;
class AcceleratorFilter;
class AcceleratorService;
class DBusAcceleratorDelegate;
class HotCornerController;
class HotCornerSettings;
} // namespace accelerators

namespace shell {
class DockWindow;
class Wallpaper;
class Shell;
struct ScreenCaptureServices;
}

namespace switcher {
class SwitcherView;
}

namespace wm {
class DemoController;
class DeskPreferences;
class QuitAll;
class WindowCycleController;
class WmGestureHandler;
} // namespace wm

namespace scene {
class DeskSwitchNode;
class OverviewDeskBarNode;
class OverviewNode;
class SlideOutNode;
} // namespace scene

namespace xwayland {
class XwaylandServer;
}

namespace core {

// Members are constructed in declaration order and destroyed in reverse, so the
// order is the lifetime contract.
class CompositorShell : private decoration::ThemeObserver, private input::SeatObserver
{
public:
    explicit CompositorShell(const CompositorOptions &options);
    CompositorShell(const CompositorShell &) = delete;
    CompositorShell &operator=(const CompositorShell &) = delete;
    ~CompositorShell();

    // False is fatal; the reason has already been logged.
    bool init();

    void stop();

    std::string socketName() const;

    bool notifyReady();

private:
    void onThemeChanged() override;

    bool initWayland();
    bool initHeadless();
    void initKeyboard();
    void initComputerControl();
    void initAccelerators();
    bool initDrm();
    bool initDisplay();
    void initXwayland();
    void initShell();
    std::string shortcutText(const std::string &key, uint32_t modifiers, bool activeWindow);
    shell::ScreenCaptureServices screenCaptureServices(shell::DockWindow *dockWindow, shell::Wallpaper *wallpaper);
    std::vector<compositor::Output *> outputModels() const;
    void publishDisplayState();
    void probeCursor();
    void releaseCursor();
    void applyScale();

    // Recomputed once per event-loop turn, after every pending scene mutation.
    void scheduleSceneSync();
    void syncSceneToClients();

    // Must run before the transition's first frame, so it observes the state change.
    class SecureScanoutPolicy : public session::SessionObserver
    {
    public:
        void setRuntime(display::DisplayRuntime *runtime);
        void onSessionStateChanged(session::SessionState state) override;

    private:
        display::DisplayRuntime *m_runtime = nullptr;
        bool m_blocked = false;
    };

    // Clients' idle inhibitors do not keep a blocked session's screen on.
    class IdleInhibitionPolicy : public session::SessionObserver
    {
    public:
        void setInhibition(wayland::IdleInhibition *inhibition);
        void onSessionStateChanged(session::SessionState state) override;

    private:
        base::WeakPtr<wayland::IdleInhibition> m_inhibition;
        bool m_blocked = false;
    };

    // A blocked session ends an agent's control, and a pending request for it.
    class ComputerControlPolicy : public session::SessionObserver
    {
    public:
        void setController(computer_control::Controller *controller);
        void onSessionStateChanged(session::SessionState state) override;

    private:
        computer_control::Controller *m_controller = nullptr;
    };

    const CompositorOptions m_options;

    // Declared first so they outlive everything holding GL resources.
    std::unique_ptr<backend::LibseatSession> m_session;
    std::unique_ptr<backend::DrmBackend> m_backend;
    std::unique_ptr<backend::LibinputBackend> m_inputBackend;
    std::unique_ptr<display::CallbackNativeDisplayDelegate> m_displayDelegate;
    std::unique_ptr<display::DisplayConfigurationController> m_displayController;
    std::unique_ptr<display::NightLight> m_nightLight;
    std::unique_ptr<display::ipc::DisplayConfigService> m_displayService;

    scene::Scene m_scene;
    scene::Node *m_surfaceContainer = nullptr;
    // Above every window and its popups, below the lock screen.
    scene::Node *m_inputMethodContainer = nullptr;
    // The application switcher, over every window and below the lock screen.
    scene::Node *m_switcherContainer = nullptr;
    // The outgoing frame of a display change covers the session, not the lock screen.
    scene::ScreenTransitionNode *m_screenTransition = nullptr;
    // The session's content is below this and the lock screen inside it; input,
    // activation and capture test membership, not z-order.
    scene::Node *m_secureContainer = nullptr;
    // The previous DRM master's last frame; only the cursor is above it.
    scene::ScreenTransitionNode *m_startupCover = nullptr;
    scene::Node *m_cursorContainer = nullptr;

    compositor::Compositor m_compositor;
    input::Seat m_seat;
    // Drag icons stay visible while cursor overrides hide client cursors.
    scene::Node *m_dragIconContainer = nullptr;
    scene::Node *m_clientCursorContainer = nullptr;
    cursor::Cursor m_cursor;
    cursor::Preferences m_cursorPreferences;
    std::unique_ptr<cursor::OutputCursorRouter> m_cursorRouter;
    input::InputManager m_inputManager;
    wm::WindowManager m_windowManager;
    display::DisplayManager m_displayManager;
    base::Subscription m_secureOutputsUpdate;
    // Declared after the backend, so its textures are freed while the GL
    // context is still alive.
    decoration::Theme m_decorationTheme;
    decoration::ThemePreferences m_themePreferences;
    decoration::DecorationProvider m_decorationProvider;
    decoration::ShadowProvider m_shadowProvider;
    wm::WmShadowControllerDelegate m_shadowDelegate;
    wm::ShadowController m_shadowController;
    wm::DockLayoutManager m_dockLayout;
    wm::DockPreferences m_dockPreferences;
    session::SecureContainer m_secureScene;
    session::SessionTeardown m_sessionTeardown;
    session::SessionController m_sessionController;
    SecureScanoutPolicy m_secureScanoutPolicy;
    IdleInhibitionPolicy m_idleInhibitionPolicy;
    ComputerControlPolicy m_computerControlPolicy;
    auth::AuthUi m_authUi;
    base::Subscription m_authCursor;
    session::ScreenSaverService m_screenSaver;
    session::SleepLock m_sleepLock;
    input::KeyboardSettings m_keyboardSettings;
    std::vector<base::Subscription> m_inputSubscriptions;
    std::unique_ptr<wm::DemoController> m_demoController;
    // Drawn inside the surface container, above the desks' windows.
    scene::DeskSwitchNode *m_deskSwitchNode = nullptr;
    scene::SlideOutNode *m_windowMoveNode = nullptr;
    scene::OverviewNode *m_overviewNode = nullptr;
    scene::OverviewDeskBarNode *m_overviewBarNode = nullptr;
    std::unique_ptr<wm::DeskPreferences> m_deskPreferences;
    std::unique_ptr<wm::WmGestureHandler> m_gestureHandler;
    // Keyboard shortcuts sit in front of the window manager in the event sink
    // chain, so they are destroyed before it.
    std::unique_ptr<wm::WindowCycleController> m_windowCycle;
    std::unique_ptr<switcher::SwitcherView> m_switcherView;
    // None in the greeter.
    std::unique_ptr<shell::Shell> m_shell;
    shell::Wallpaper *m_wallpaper = nullptr;
    std::unique_ptr<accelerators::AcceleratorConfiguration> m_acceleratorConfiguration;
    std::unique_ptr<accelerators::DBusAcceleratorDelegate> m_acceleratorDelegate;
    std::unique_ptr<accelerators::AcceleratorController> m_acceleratorController;
    std::unique_ptr<accelerators::AcceleratorFilter> m_acceleratorFilter;
    std::unique_ptr<accelerators::AcceleratorService> m_acceleratorService;
    // The settings go first, and with them the connections feeding the corners.
    std::unique_ptr<accelerators::HotCornerController> m_hotCorners;
    std::unique_ptr<accelerators::HotCornerSettings> m_hotCornerSettings;
    std::vector<base::Subscription> m_windowManagerSignals;
    base::Subscription m_hotCornerBindings;
    base::Subscription m_hotCornerPointer;
    // In front of the whole chain, so the key reaches neither a shortcut nor a client.
    std::unique_ptr<session::PowerButton> m_powerButton;
    std::unique_ptr<wm::QuitAll> m_quitAll;
    std::unique_ptr<session::ApplicationsService> m_applicationsService;
    std::unique_ptr<session::WindowDebugService> m_windowDebugService;
    // The consent outlives the controller, which goes before the input manager it
    // injects into.
    std::unique_ptr<computer_control::RememberedApps> m_computerControlApps;
    std::unique_ptr<computer_control::PermissionConsent> m_computerControlConsent;
    std::unique_ptr<computer_control::Controller> m_computerControl;

    compositor::Output m_outputModel;
    std::vector<std::unique_ptr<compositor::Output>> m_additionalOutputModels;
    std::vector<wl_global *> m_outputGlobals;
    wl_global *m_outputGlobal = nullptr;
    bool m_updatingOutput = false;
    // In the native pixels of its output.
    struct ProbedCursor {
        display::DisplayId destination;
        base::Point topLeft;
    };
    std::optional<ProbedCursor> m_probedCursor;
    bool m_cursorProbed = false;
    bool m_cursorHeld = false;
    base::Timer m_cursorRelease {mainLoop()};
    base::Timer m_sceneSync {mainLoop()};
    std::string m_compositorInstanceId;

    std::unique_ptr<wayland::ExecutableSecurityDelegate> m_security;

    void onSurfaceFocused(compositor::Surface *, compositor::Surface *) override { }
    void onCursorChanged() override;
    void onCursorPositionChanged() override;
    void onKeyboardLayoutUpdated() override;
    std::unique_ptr<wayland::Server> m_server;
    std::unique_ptr<wayland::LinuxDmabufGlobal> m_dmabufGlobal;
    std::unique_ptr<wayland::DmaBufScanoutPolicy> m_scanoutPolicy;
    base::Subscription m_scanoutTranchesUpdate;
    wl_global *m_syncobjGlobal = nullptr;
    std::unique_ptr<wayland::ProtocolGlobals> m_globals;
    std::unique_ptr<xwayland::XwaylandServer> m_xwayland;
    std::unique_ptr<wayland::ShellControlChannel> m_shellControl;
    std::unique_ptr<backend::HeadlessBackend> m_headless;

    std::unique_ptr<display::DisplayRuntime> m_displayRuntime;
    std::unique_ptr<display::DisplayTopologyCoordinator> m_topologyCoordinator;
    // On the backends' and the topology coordinator's signals; they go first.
    std::vector<base::Subscription> m_backendSubscriptions;

    // Last, so they go before any receiver they call.
    base::Subscription m_computerControlDrawn;
    base::Subscription m_menuBarAgentControl;
    std::vector<base::Subscription> m_shellSubscriptions;
    std::vector<base::Subscription> m_compositorSubscriptions;
};

} // namespace core
} // namespace cutefish

#endif // CUTEFISH_CORE_COMPOSITOR_SHELL_H_
