// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "wm/window_observer.h"

struct wl_client;
struct wl_display;
struct wl_global;
struct wl_resource;

namespace cutefish::compositor {
class CaptureSource;
class Compositor;
}

namespace cutefish::wm {
class Window;
class WindowManager;
}

namespace cutefish::wayland {

// Only announced to clients allowed to capture.
class ForeignToplevelList : private wm::WindowObserver
{
public:
    using Authorize = std::function<bool(const wl_client *)>;
    using ShadowBounds = std::function<base::Rect(const wm::Window *)>;

    ForeignToplevelList(wl_display *display, wm::WindowManager *manager,
                        compositor::Compositor *compositor, Authorize authorize);
    ~ForeignToplevelList() override;

    // Before the display goes.
    void shutdown();

    wl_global *global() const { return m_global; }

    // The capture source behind a handle resource, or null for a handle of
    // another client's list or a closed window.
    std::shared_ptr<compositor::CaptureSource> sourceFor(wl_resource *handle) const;

    // Everything the window paints, shadow and outline included, in global
    // logical coordinates; unset, a shadowed source covers the window bounds.
    void setShadowBounds(ShadowBounds bounds);

    // Any live window has one, listed or not; its bounds are refreshed on each call.
    std::shared_ptr<compositor::CaptureSource> shadowedSourceFor(wm::Window *window);

    int boundListCount() const { return int(m_clients.size()); }

private:
    struct Client;
    struct Entry;
    friend struct ForeignToplevelListEvents;

    static bool listable(const wm::Window *window);
    Entry &entryFor(wm::Window *window);
    void add(wm::Window *window);
    void remove(wm::Window *window);
    void announce(Client &client, Entry &entry);
    void updateGeometry(Entry &entry);
    void updateAllGeometry();

    void onWindowBoundsChanged(wm::Window *window, const base::Rect &oldBounds) override;
    void onWindowStateTypeChanged(wm::Window *window, wm::WindowStateType oldType) override;
    void onWindowApplicationIdChanged(wm::Window *window) override;
    void onWindowTitleChanged(wm::Window *window) override;

    wm::WindowManager *m_manager;
    compositor::Compositor *m_compositor;
    Authorize m_authorize;
    ShadowBounds m_shadowBounds;
    wl_global *m_global = nullptr;
    std::vector<std::unique_ptr<Client>> m_clients;
    std::map<wm::Window *, std::unique_ptr<Entry>> m_entries;
    base::Subscription m_outputViewsChanged;
    base::Subscription m_windowAdded;
    base::Subscription m_windowRemoved;
};

} // namespace cutefish::wayland
