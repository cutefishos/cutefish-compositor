// Copyright 2012 The Chromium Authors
// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/wm/core/shadow_controller.{h,cc}
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WM_SHADOW_CONTROLLER_H_
#define CUTEFISH_WM_SHADOW_CONTROLLER_H_

#include <map>
#include <memory>


#include "base/geometry.h"
#include "base/signal.h"
#include "wm/shadow_types.h"
#include "wm/window_observer.h"
#include "wm/window_shadow.h"

namespace cutefish::wm {

class ShadowControllerDelegate;
class Window;
class WindowManager;

// Must be destroyed before the WindowManager it observes.
class ShadowController : public WindowObserver
{
public:
    // `provider` may be null, which leaves every window without a shadow.
    ShadowController(WindowManager* manager, ShadowProvider* provider,
                     ShadowControllerDelegate* delegate);
    ~ShadowController() override;

    // Null until the window has one.
    WindowShadow* shadowForWindow(const Window* window) const;

    ShadowElevation elevationForWindow(const Window* window) const;

    void handlePossibleShadowVisibilityChange(Window* window);

    void onWindowBoundsChanged(Window* window, const base::Rect& oldBounds) override;
    void onWindowStateTypeChanged(Window* window, WindowStateType oldType) override;
    void onWindowActivationChanged(Window* window, bool active) override;
    void onWindowVisibilityChanged(Window* window, bool visible) override;
    void onWindowContentChanged(Window* window) override;
    void onWindowDragStateChanged(Window* window, bool dragged) override;
    void onWindowClientDecorationChanged(Window* window) override;
    void onWindowFrameChanged(Window* window) override;

private:
    void onWindowAdded(cutefish::wm::Window* window);
    void onWindowRemoved(cutefish::wm::Window* window);

private:
    WindowShadow* createShadowForWindow(Window* window);

    WindowManager* m_manager = nullptr;
    ShadowProvider* m_provider = nullptr;
    ShadowControllerDelegate* m_delegate = nullptr;
    std::map<Window*, std::unique_ptr<WindowShadow>> m_shadows;
    base::Subscription m_windowAdded;
    base::Subscription m_windowRemoved;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_SHADOW_CONTROLLER_H_
