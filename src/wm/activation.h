// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/wm/core/focus_controller.* and base_focus_rules.*
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WM_ACTIVATION_H_
#define CUTEFISH_WM_ACTIVATION_H_

#include <vector>

namespace cutefish::wm {

class Window;
class WindowManager;

class ActivationObserver
{
public:
    virtual ~ActivationObserver() = default;

    // Called before a change, while no other activation change can start.
    virtual void onWindowActivating(Window *gainingActive, Window *losingActive) { }

    // Called once per change, after both windows have been told. Either may be null.
    virtual void onWindowActivated(Window *gainedActive, Window *lostActive) = 0;
};

class Activation
{
public:
    explicit Activation(WindowManager *manager);
    Activation(const Activation &) = delete;
    Activation &operator=(const Activation &) = delete;
    ~Activation();

    Window *activeWindow() const { return m_activeWindow; }

    // Null clears activation. See activatableFor().
    void activate(Window *window);

    void deactivate(Window *window);

    bool canActivate(const Window *window) const;

    // Its modal transient if blocked by one, else the nearest activatable transient ancestor.
    Window *activatableFor(Window *window) const;

    void addObserver(ActivationObserver *observer);
    void removeObserver(ActivationObserver *observer);

    // Called by WindowManager just before `window` is destroyed.
    void onWindowRemoving(Window *window);

private:
    void setActiveWindow(Window *window);
    Window *nextActivatableWindow(Window *ignore) const;

    WindowManager *m_manager = nullptr;
    Window *m_activeWindow = nullptr;
    std::vector<ActivationObserver *> m_observers;
    bool m_updating = false;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_ACTIVATION_H_
