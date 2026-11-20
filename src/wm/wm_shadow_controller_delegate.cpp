// Copyright 2019 The Chromium Authors
// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/wm_shadow_controller_delegate.{h,cc}
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include "wm/wm_shadow_controller_delegate.h"

#include "compositor/surface.h"
#include "wm/window.h"

namespace cutefish::wm {

bool WmShadowControllerDelegate::shouldShowShadowForWindow(const Window *window)
{
    if (!window || !window->isVisible() || window->bounds().isEmpty())
        return false;

    if (const auto *surface = window->rootSurface(); surface && !surface->castsShadow())
        return false;

    if ((window->isPopup() || window->role() != WindowRole::Normal) && !window->isDecorated()
        && window->hasClientExteriorDecoration())
        return false;

    if (window->isPopup() && !window->isDecorated() && window->hasClientInputMargin())
        return false;

    if (window->isPopup()) {
        const auto *surface = window->rootSurface();
        return surface && (!surface->backgroundBlurRect().isEmpty() || !surface->bodyShape().isEmpty());
    }

    switch (window->role()) {
    case WindowRole::Normal:
        break;
    case WindowRole::Dock:
    case WindowRole::Notification:
    case WindowRole::Osd:
    case WindowRole::Sheet:
    case WindowRole::Picker:
        return true;
    case WindowRole::Desktop:
    case WindowRole::Panel:
    case WindowRole::Launchpad:
    case WindowRole::Capture:
        return false;
    }

    return isRoundedStateType(window->stateType());
}

ShadowElevation WmShadowControllerDelegate::elevationForWindow(const Window *window)
{
    if (window->isPopup())
        return ShadowElevation::MenuOrTooltip;
    if (window->role() == WindowRole::Dock)
        return ShadowElevation::Dock;
    if (window->role() == WindowRole::Notification || window->role() == WindowRole::Osd
        || window->role() == WindowRole::Picker)
        return ShadowElevation::Popup;
    if (window->isDragged())
        return ShadowElevation::Active;
    return window->isActive() ? ShadowElevation::Active : ShadowElevation::Inactive;
}

} // namespace cutefish::wm
