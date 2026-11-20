// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_AUTH_UI_H
#define CUTEFISH_AUTH_UI_H
#include "base/dbus.h"
#include "base/geometry.h"
#include "base/signal.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "input/cursor_shape.h"
#include "input/event_sink.h"
#include "session/secure_container.h"
#include "session/session_state.h"
#include "ui/bitmap.h"
#include <ctime>
#include <optional>
#include <functional>
#include <memory>
#include <vector>

namespace cutefish::input {
class XkbTracker;
}

namespace cutefish::render {
class Renderer;
class Texture;
} // namespace cutefish::render

namespace cutefish::toolkit {
class PanelTexture;
}

namespace cutefish::scene {
class Node;
}

namespace cutefish::ui {
class EmbeddedWindow;
class View;
} // namespace cutefish::ui

namespace cutefish::session {
class SessionController;
}

namespace cutefish::auth {
class AuthBackend;
class AuthController;
class AuthScreen;
class AuthStatus;

class AuthUi : public input::EventSink, public session::SessionObserver
{
public:
    AuthUi(session::SessionController *session, const input::XkbTracker *xkb,
           AuthBackend *backend = nullptr);
    ~AuthUi() override;
    void onSessionStateChanged(session::SessionState state) override;
    void onSessionOutputsChanged() override;
    bool onKey(input::Timestamp, uint32_t code, bool pressed) override;
    bool onPointerMotion(input::Timestamp, const base::PointF &position) override;
    bool onPointerButton(input::Timestamp, const base::PointF &position, uint32_t button,
                         bool pressed) override;

    ui::EmbeddedWindow *window() const { return m_window.get(); }
    ui::View *focusView() const;

    AuthController *controller() const { return m_controller.get(); }

    // Off for a user who has no password; the login screen always asks for one.
    void setPasswordRequired(bool required) { m_passwordRequired = required; }

    // Nothing queues one while the panel is idle.
    bool isRefreshPending() const { return m_refresh.isActive(); }

    bool isPanelDrawn() const { return m_window && m_sized; }

    uint64_t imageGeneration() const { return m_imageGeneration; }

    void setPointerLocator(std::function<base::PointF()> locator)
    {
        m_pointerLocator = std::move(locator);
    }

    void setKeyRepeatSettings(const input::KeyRepeatSettings &settings)
    {
        m_keyRepeat = settings;
    }

    // In panel coordinates.
    base::RectF fieldBounds() const;
    double fieldOpacity() const;

    ui::Bitmap renderImage();
    render::Texture *panelTexture(render::Renderer &renderer);
    void releaseRenderResources();

    ui::EmbeddedWindow *statusWindow() const { return m_statusWindow.get(); }
    AuthStatus *status() const { return m_status; }
    ui::Bitmap renderStatusImage();
    render::Texture *statusTexture(render::Renderer &renderer);
    void releaseStatusRenderResources();
    void refresh();
    base::Signal<input::CursorShape> cursorShapeChanged;

private:
    void clear();
    void scheduleRefresh();
    void refreshStatus(const session::SecureOutput &target);
    void judgeBackdrop();
    const session::SecureOutput &
    panelOutput(const std::vector<session::SecureOutput> &outputs) const;
    bool sendKey(uint32_t code, bool pressed, bool repeat);
    void updateCursor();
    session::SessionController *m_session;
    const input::XkbTracker *m_xkb;
    AuthBackend *m_injectedBackend;
    base::Timer m_refresh;
    base::Timer m_repeatTimer;
    // logind's Lock for this session.
    base::dbus::Match m_lockRequested;
    input::KeyRepeatSettings m_keyRepeat;
    uint32_t m_repeatCode = 0;
    ui::View *m_repeatFocus = nullptr;
    base::PointF m_pointerPosition;
    std::function<base::PointF()> m_pointerLocator;
    std::string m_panelOutput;
    input::CursorShape m_cursorShape = input::CursorShape::Default;
    bool m_started = false;
    bool m_passwordRequired = true;
    std::unique_ptr<AuthBackend> m_backend;
    std::unique_ptr<AuthController> m_controller;
    std::vector<base::Subscription> m_controllerSubscriptions;
    std::unique_ptr<ui::EmbeddedWindow> m_window;
    std::unique_ptr<toolkit::PanelTexture> m_panel;
    AuthScreen *m_screen = nullptr;
    input::CursorShape m_viewCursor = input::CursorShape::Default;
    double m_scale = 1;
    bool m_sized = false;
    scene::Node *m_node = nullptr;
    bool m_pointerInside = false;
    bool m_attempted = false;
    bool m_authenticated = false;
    bool m_focused = false;
    bool m_resetPending = false;
    session::SessionState m_state = session::SessionState::Active;
    bool m_dirty = true;
    uint64_t m_imageGeneration = 0;
    // Kept from lock to lock, unlike the panel.
    std::unique_ptr<ui::EmbeddedWindow> m_statusWindow;
    std::unique_ptr<toolkit::PanelTexture> m_statusPanel;
    AuthStatus *m_status = nullptr;
    scene::Node *m_statusNode = nullptr;
    double m_statusScale = 1;
    bool m_statusDirty = true;
    base::Subscription m_wallpaperLoaded;
    base::Job m_judgement;
    std::string m_judgedOutput;
    const void *m_judgedPixels = nullptr;
    bool m_judged = false;
};
} // namespace cutefish::auth
#endif
