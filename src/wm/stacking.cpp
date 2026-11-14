// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wm/stacking.h"

#include "wm/window.h"

namespace cutefish::wm {

StackingLayer layerFor(WindowRole role, PanelBehavior behavior)
{
    switch (role) {
    case WindowRole::Desktop:
        return StackingLayer::Desktop;
    case WindowRole::Normal:
        return StackingLayer::Normal;
    case WindowRole::Launchpad:
        return StackingLayer::Launchpad;
    case WindowRole::Panel:
        return behavior == PanelBehavior::WindowsCanCover ? StackingLayer::Normal
                                                          : StackingLayer::Panel;
    case WindowRole::Dock:
        return behavior == PanelBehavior::WindowsCanCover ? StackingLayer::Normal
                                                          : StackingLayer::Dock;
    case WindowRole::Notification:
        return StackingLayer::Notification;
    case WindowRole::Osd:
        return StackingLayer::Osd;
    case WindowRole::Picker:
        return StackingLayer::Picker;
    case WindowRole::Capture:
        return StackingLayer::Capture;
    case WindowRole::Sheet:
        return StackingLayer::Normal;
    }
    return StackingLayer::Normal;
}

StackingLayer layerFor(const Window *window)
{
    if (!window)
        return StackingLayer::Normal;
    if (window->role() == WindowRole::Sheet && window->transientParent())
        return layerFor(window->transientParent());
    if (window->isPopup() && window->transientParent()) {
        const StackingLayer parentLayer = layerFor(window->transientParent());
        return parentLayer == StackingLayer::Desktop ? StackingLayer::DesktopPopup : parentLayer;
    }
    if (window->isAlwaysOnTop() && window->role() == WindowRole::Normal)
        return StackingLayer::AlwaysOnTop;
    // A dialog stays with the window it belongs to, as transient children
    // share their parent's container in ash.
    if (window->role() == WindowRole::Normal && window->transientParent()
        && layerFor(window->transientParent()) == StackingLayer::AlwaysOnTop)
        return StackingLayer::AlwaysOnTop;
    return layerFor(window->role(), window->panelBehavior());
}

} // namespace cutefish::wm
