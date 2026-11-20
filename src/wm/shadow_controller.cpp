// Copyright 2012 The Chromium Authors
// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/wm/core/shadow_controller.{h,cc}
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include "wm/shadow_controller.h"

#include "wm/shadow_controller_delegate.h"
#include "wm/window.h"
#include "wm/window_manager.h"

namespace cutefish::wm {

ShadowController::ShadowController(WindowManager* manager, ShadowProvider* provider,
                                   ShadowControllerDelegate* delegate)
    : m_manager(manager)
    , m_provider(provider)
    , m_delegate(delegate)
{
    m_windowAdded = m_manager->windowAdded.connect([this](Window* window) {
        onWindowAdded(window);
    });
    m_windowRemoved = m_manager->windowRemoved.connect([this](Window* window) {
        onWindowRemoved(window);
    });
    for (Window* window : m_manager->windows())
        onWindowAdded(window);
}

ShadowController::~ShadowController()
{
    for (const auto& [window, shadow] : m_shadows)
        window->removeObserver(this);
    m_shadows.clear();
}

WindowShadow* ShadowController::shadowForWindow(const Window* window) const
{
    const auto it = m_shadows.find(const_cast<Window*>(window));
    return it == m_shadows.end() ? nullptr : it->second.get();
}

ShadowElevation ShadowController::elevationForWindow(const Window* window) const
{
    if (!window || !m_delegate || !m_delegate->shouldShowShadowForWindow(window))
        return ShadowElevation::None;
    return m_delegate->elevationForWindow(window);
}

void ShadowController::onWindowAdded(Window* window)
{
    if (!window || m_shadows.count(window) || !createShadowForWindow(window))
        return;
    window->addObserver(this);
    handlePossibleShadowVisibilityChange(window);
}

void ShadowController::onWindowRemoved(Window* window)
{
    // The window's scene node still lives here, but not by onWindowDestroying.
    const auto it = m_shadows.find(window);
    if (it == m_shadows.end())
        return;
    window->removeObserver(this);
    m_shadows.erase(it);
}

WindowShadow* ShadowController::createShadowForWindow(Window* window)
{
    if (!m_provider)
        return nullptr;
    std::unique_ptr<WindowShadow> shadow = m_provider->createShadow(window);
    if (!shadow)
        return nullptr;
    WindowShadow* raw = shadow.get();
    m_shadows.emplace(window, std::move(shadow));
    raw->stackAtBottom();
    return raw;
}

void ShadowController::handlePossibleShadowVisibilityChange(Window* window)
{
    WindowShadow* shadow = shadowForWindow(window);
    if (!shadow)
        return;
    shadow->setContentBounds(window->bounds());
    shadow->setElevation(elevationForWindow(window));
}

void ShadowController::onWindowBoundsChanged(Window* window, const base::Rect&)
{
    handlePossibleShadowVisibilityChange(window);
}

void ShadowController::onWindowStateTypeChanged(Window* window, WindowStateType)
{
    handlePossibleShadowVisibilityChange(window);
}

void ShadowController::onWindowActivationChanged(Window* window, bool)
{
    handlePossibleShadowVisibilityChange(window);
}

void ShadowController::onWindowVisibilityChanged(Window* window, bool)
{
    handlePossibleShadowVisibilityChange(window);
}

void ShadowController::onWindowContentChanged(Window* window)
{
    handlePossibleShadowVisibilityChange(window);
}

void ShadowController::onWindowDragStateChanged(Window* window, bool)
{
    handlePossibleShadowVisibilityChange(window);
}

void ShadowController::onWindowClientDecorationChanged(Window* window)
{
    handlePossibleShadowVisibilityChange(window);
}

void ShadowController::onWindowFrameChanged(Window* window)
{
    // A frame added after mapping goes to the bottom of the window, so the shadow follows.
    if (WindowShadow* shadow = shadowForWindow(window))
        shadow->stackAtBottom();
    handlePossibleShadowVisibilityChange(window);
}

} // namespace cutefish::wm
