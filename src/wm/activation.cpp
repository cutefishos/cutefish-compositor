// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/wm/core/focus_controller.* and base_focus_rules.*
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include "wm/activation.h"

#include <algorithm>

#include "wm/window.h"
#include "wm/window_manager.h"
#include "wm/window_modality_controller.h"
#include "wm/window_state.h"

namespace cutefish::wm {

Activation::Activation(WindowManager *manager)
    : m_manager(manager)
{
}

Activation::~Activation() = default;

bool Activation::canActivate(const Window *window) const
{
    if (!window)
        return true;
    if (!m_manager->allowsActivation())
        return false;
    if (window->isPopup() || window->focusPolicy() == FocusPolicy::NoFocus)
        return false;
    if (window->role() == WindowRole::Osd)
        return false;
    if (window->role() == WindowRole::Capture || window->role() == WindowRole::Picker)
        return false;
    if (window->isDragPreview())
        return false;
    // A window on an inactive desk counts as visible: activating it switches to its desk.
    if (!window->targetVisibility() || window->state()->isMinimized())
        return false;
    return !modalTransient(window);
}

Window *Activation::activatableFor(Window *window) const
{
    if (!window || canActivate(window))
        return window;

    if (Window *modal = modalTransient(window))
        return activatableFor(modal);

    if (Window *parent = window->transientParent()) {
        // Upstream returns `window` here, which would get past a locked session.
        if (modalTransient(parent) == window)
            return nullptr;
        return activatableFor(parent);
    }
    return nullptr;
}

void Activation::activate(Window *window)
{
    if (m_updating)
        return;

    Window *target = window ? activatableFor(window) : nullptr;
    if (window && !target)
        return;

    setActiveWindow(target);
}

void Activation::deactivate(Window *window)
{
    if (!window || window != m_activeWindow || m_updating)
        return;
    setActiveWindow(nextActivatableWindow(window));
}

void Activation::onWindowRemoving(Window *window)
{
    if (window != m_activeWindow)
        return;
    setActiveWindow(nextActivatableWindow(window));
}

Window *Activation::nextActivatableWindow(Window *ignore) const
{
    const std::vector<Window *> &windows = m_manager->windows();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        if (*it == ignore)
            continue;
        if ((*it)->isVisible() && canActivate(*it))
            return *it;
    }
    return nullptr;
}

void Activation::setActiveWindow(Window *window)
{
    if (window == m_activeWindow)
        return;

    m_updating = true;
    for (ActivationObserver *observer : std::vector<ActivationObserver *>(m_observers)) {
        if (std::find(m_observers.begin(), m_observers.end(), observer) != m_observers.end())
            observer->onWindowActivating(window, m_activeWindow);
    }
    Window *lostActive = m_activeWindow;
    m_activeWindow = window;

    // Deactivate first, so an observer never sees two active windows.
    if (lostActive)
        lostActive->setActiveInternal(false);
    if (m_activeWindow) {
        m_activeWindow->setActiveInternal(true);
        m_manager->raise(m_activeWindow);
    }
    m_updating = false;

    for (ActivationObserver *observer : std::vector<ActivationObserver *>(m_observers)) {
        if (std::find(m_observers.begin(), m_observers.end(), observer) != m_observers.end())
            observer->onWindowActivated(m_activeWindow, lostActive);
    }
}

void Activation::addObserver(ActivationObserver *observer)
{
    if (std::find(m_observers.begin(), m_observers.end(), observer) == m_observers.end())
        m_observers.push_back(observer);
}

void Activation::removeObserver(ActivationObserver *observer)
{
    m_observers.erase(std::remove(m_observers.begin(), m_observers.end(), observer),
                      m_observers.end());
}

} // namespace cutefish::wm
