// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_STACKING_H_
#define CUTEFISH_WM_STACKING_H_

#include "wm/types.h"

namespace cutefish::wm {

class Window;

// No raise or activation may move a window out of the layer its role puts it in.
enum class StackingLayer {
    Desktop = 0,
    Normal = 1,
    AlwaysOnTop = 2,
    DesktopPopup = 3,
    Panel = 4,
    Launchpad = 5,
    Dock = 6,
    AboveDock = 7,
    Notification = 8,
    Osd = 9,
    Picker = 10,
    Capture = 11,
};

// A panel whose behavior is WindowsCanCover drops into the normal layer.
StackingLayer layerFor(WindowRole role, PanelBehavior behavior);
StackingLayer layerFor(const Window *window);

} // namespace cutefish::wm

#endif // CUTEFISH_WM_STACKING_H_
