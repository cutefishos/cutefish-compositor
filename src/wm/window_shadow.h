// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_WINDOW_SHADOW_H_
#define CUTEFISH_WM_WINDOW_SHADOW_H_

#include <memory>

#include "base/geometry.h"
#include "wm/shadow_types.h"

namespace cutefish::wm {

class Window;

class WindowShadow
{
public:
    virtual ~WindowShadow() = default;

    // ShadowElevation::None hides the shadow without destroying it.
    virtual void setElevation(ShadowElevation elevation) = 0;
    virtual ShadowElevation elevation() const = 0;

    // In output coordinates.
    virtual void setContentBounds(const base::Rect& bounds) = 0;
    virtual base::Rect contentBounds() const = 0;

    // In output coordinates; empty when nothing is drawn.
    virtual base::Rect shadowBounds() const = 0;

    virtual void stackAtBottom() = 0;
};

class ShadowProvider
{
public:
    virtual ~ShadowProvider() = default;

    // May return null for a window with no scene node.
    virtual std::unique_ptr<WindowShadow> createShadow(Window* window) = 0;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_SHADOW_H_
