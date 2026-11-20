// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WAYLAND_PROTOCOL_GLOBALS_H_
#define CUTEFISH_WAYLAND_PROTOCOL_GLOBALS_H_

#include <memory>
#include <string>

#include "base/signal.h"

#include "base/signal.h"

namespace cutefish {

namespace compositor {
class Compositor;
}

namespace computer_control {
class Controller;
}

namespace input {
class InputMethodSettings;
class Seat;
class InputManager;
} // namespace input

namespace scene {
class Node;
}

namespace wm {
class ShadowController;
class WindowManager;
}

namespace wayland {

class DataControlManager;
class ForeignToplevelList;
class IdleInhibition;
class ImageCaptureManager;
class InputMethodLauncher;
class InputMethodManager;
class PrivilegedClients;
class SecurityDelegate;
class Server;
class WaylandComputerControl;
class WaylandWindowPlacement;
class WaylandCutefishShell;
class WaylandXdgActivation;
class WaylandXdgShell;
struct WaylandSeat;

// Globals that depend on the graphics device are registered by whoever owns that device.
class ProtocolGlobals
{
public:
    struct Dependencies {
        Server *server = nullptr;
        compositor::Compositor *compositor = nullptr;
        input::Seat *seat = nullptr;
        input::InputManager *inputManager = nullptr;
        wm::WindowManager *windowManager = nullptr;
        // Null leaves shadowed window captures at the window bounds.
        wm::ShadowController *shadowController = nullptr;
        // Candidate popups stack here, above every window but below the lock
        // screen.
        scene::Node *inputMethodPopupLayer = nullptr;
        SecurityDelegate *security = nullptr;
        // Null leaves computer control out.
        computer_control::Controller *computerControl = nullptr;
        // The greeter and development instances must not take over the user's
        // input method.
        bool launchInputMethod = false;
        // Empty means use the saved selection.
        std::string inputMethodOverride;
    };

    explicit ProtocolGlobals(const Dependencies &dependencies);
    ProtocolGlobals(const ProtocolGlobals &) = delete;
    ProtocolGlobals &operator=(const ProtocolGlobals &) = delete;
    ~ProtocolGlobals();

    // Call once the server is listening. On false the reason has already been logged.
    bool initialize();

    // Releases the globals. Must run after the clients are gone and before the
    // display is destroyed.
    void shutdown();

    // Spawns the input method once, without an X11 display. Later calls are
    // ignored.
    void startInputMethod();
    // Restarts a started input method with this X11 display: Xwayland started
    // on demand after it, or restarted after a crash.
    void setInputMethodX11Display(const std::string &x11Display,
                                  const std::string &x11Authority);

    void sceneChanged();

    PrivilegedClients *privileged() { return m_privileged.get(); }

    IdleInhibition *idleInhibition() { return m_idleInhibition.get(); }

    ImageCaptureManager *imageCapture() { return m_imageCapture.get(); }

    ForeignToplevelList *toplevelList() { return m_toplevelList.get(); }

    InputMethodLauncher *inputMethodLauncher() { return m_inputMethodLauncher.get(); }

    InputMethodManager *inputMethod() { return m_inputMethod.get(); }

    WaylandXdgActivation *xdgActivation() { return m_xdgActivation.get(); }

private:
    bool registerCoreGlobals();
    bool registerInputMethod();
    bool registerClipboard();
    bool registerCapture();
    bool registerComputerControl();
    bool registerWindowPlacement();
    bool registerPrivateShell();

    const Dependencies m_deps;
    bool m_inputMethodStarted = false;

    // Declaration order is destruction order reversed, and it matters.
    std::unique_ptr<PrivilegedClients> m_privileged;
    std::unique_ptr<WaylandSeat> m_seat;
    std::unique_ptr<IdleInhibition> m_idleInhibition;
    std::unique_ptr<WaylandXdgActivation> m_xdgActivation;
    std::unique_ptr<WaylandXdgShell> m_xdgShell;
    std::unique_ptr<InputMethodManager> m_inputMethod;
    std::unique_ptr<InputMethodLauncher> m_inputMethodLauncher;
    std::unique_ptr<input::InputMethodSettings> m_inputMethodSettings;
    base::Subscription m_launcherRunning;
    base::Subscription m_inputMethodCommand;
    std::unique_ptr<DataControlManager> m_dataControl;
    std::unique_ptr<ImageCaptureManager> m_imageCapture;
    std::unique_ptr<ForeignToplevelList> m_toplevelList;
    std::unique_ptr<WaylandComputerControl> m_computerControl;
    std::unique_ptr<WaylandWindowPlacement> m_windowPlacement;
    std::unique_ptr<WaylandCutefishShell> m_cutefishShell;
};

} // namespace wayland
} // namespace cutefish

#endif // CUTEFISH_WAYLAND_PROTOCOL_GLOBALS_H_
