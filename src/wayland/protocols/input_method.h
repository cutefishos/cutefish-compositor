// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "base/geometry.h"
#include <functional>
#include <memory>
#include <string>
#include <wayland-server-core.h>

namespace cutefish::scene {
class Node;
}

namespace cutefish::wayland {
struct WaylandSeat;
class InputMethodRelay;

// Both in output coordinates: below the caret, above it when only there it fits, kept inside `area`.
base::Point inputPopupPosition(const base::Rect &caret, const base::Size &size,
                               const base::Rect &area);

// Owns the application/IME relay; resources must die before the seat and display.
class InputMethodManager
{
public:
    // Candidate popups are placed in `popupLayer`, in output coordinates, so they
    // stack above every window, including the application's own popups.
    InputMethodManager(wl_display *display, WaylandSeat *seat, scene::Node *popupLayer);
    ~InputMethodManager();
    InputMethodManager(const InputMethodManager &) = delete;
    InputMethodManager &operator=(const InputMethodManager &) = delete;

    // Keeps popups on the text cursor when windows move; call on scene changes.
    void sceneChanged();

    // Releases the globals; must run before the display is destroyed.
    void shutdown();

    // For a text cursor in output coordinates. Unset, popups only follow the cursor.
    void setPlacementArea(std::function<base::Rect(const base::Rect &caret)> area);

    // Restricts input-method and virtual-keyboard binds; unset allows every client.
    void setAuthorizer(std::function<bool(const wl_client *)> authorizer);

    // Other clients that may bind virtual keyboards, past the compositor's shortcuts but not while
    // locked. Unset allows none.
    void setTypists(std::function<bool(const wl_client *)> typists);

    InputMethodRelay *relay() const;

    bool hasFocusedTextInput() const;

    bool grabsKeyboard() const;

    // False, and nothing sent, without an enabled text field.
    bool commitText(const std::string &text);

    wl_global *textInputGlobal() const;
    wl_global *textInputV1Global() const;
    wl_global *inputMethodGlobal() const;
    wl_global *virtualKeyboardGlobal() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace cutefish::wayland
