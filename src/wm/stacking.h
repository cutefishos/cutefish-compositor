// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_STACKING_H_
#define CUTEFISH_WM_STACKING_H_

#include "wm/types.h"

namespace cutefish::wm {

class Window;

// The one place role-aware z-order is decided. Nothing outside this header may
// hardcode a layer number, and no raise or activation may move a window out of
// the layer its role puts it in.
//
// Bottom to top:
//
//   Desktop < Normal applications < AlwaysOnTop < DesktopPopup < Panel < Launcher < Dock < Notification < Osd < Capture
//
// Osd is the level a volume or brightness key shows, which must not hide
// behind a banner that happens to be up.
//
// Capture is the screen capture interface, which has to reach every pixel the
// user can point at, the panels and banners included.
//
// The cursor is not a window; it composites above all of this from its own
// scene container.
enum class StackingLayer {
    Desktop = 0,
    Normal = 1,
    AlwaysOnTop = 2,
    DesktopPopup = 3,
    Panel = 4,
    Launcher = 5,
    Dock = 6,
    Notification = 7,
    Osd = 8,
    Picker = 9,
    Capture = 10,
};

// A panel whose behavior is WindowsCanCover drops into the normal layer.
StackingLayer layerFor(WindowRole role, PanelBehavior behavior);
StackingLayer layerFor(const Window *window);

} // namespace cutefish::wm

#endif // CUTEFISH_WM_STACKING_H_
