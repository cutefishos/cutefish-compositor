// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/desks/desk.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65
//
// Adapted for Cutefish: a desk owns no container; Window::setDeskHidden hides its windows.

#ifndef CUTEFISH_WM_DESKS_DESK_H_
#define CUTEFISH_WM_DESKS_DESK_H_

#include <cstdint>
#include <string>
#include <vector>

namespace cutefish::wm {

class DesksController;
class Window;

class Desk
{
public:
    explicit Desk(DesksController *controller);
    Desk(const Desk &) = delete;
    Desk &operator=(const Desk &) = delete;
    ~Desk();

    // Stable for the desk's lifetime and never reused.
    uint32_t id() const { return m_id; }

    // Transient roots in the order they came; their descendants go wherever the root goes.
    const std::vector<Window *> &windows() const { return m_windows; }

    bool contains(const Window *window) const;

    // Empty while the desk has its default name, which the shell words itself.
    const std::string &name() const { return m_name; }
    bool isNameSetByUser() const { return m_nameSetByUser; }
    void setName(const std::string &name, bool setByUser);

    bool isActive() const { return m_active; }

    void activate(bool updateWindowActivation);

    void deactivate(bool updateWindowActivation);

    void moveWindowsToDesk(Desk *target);

    // `window` must be on this desk.
    void moveWindowToDesk(Window *window, Desk *target);

private:
    friend class DesksController;

    void addWindow(Window *window);
    void removeWindow(Window *window);

    DesksController *m_controller = nullptr;
    const uint32_t m_id;
    std::vector<Window *> m_windows;
    std::string m_name;
    bool m_nameSetByUser = false;
    bool m_active = false;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_DESKS_DESK_H_
