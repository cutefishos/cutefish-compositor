// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "base/geometry.h"

#include "render/renderer.h"

namespace cutefish::decoration {

// Every tunable lives in window_visual_style.cpp, so changing one only relinks.
struct WindowVisualStyle {
    // `accentIndex` is the Appearance service's AccentColor setting.
    explicit WindowVisualStyle(bool dark = false, int accentIndex = 0);

    // Clamped so the corners never overlap on a small window.
    static int radiusFor(const base::Size &size);

    int radius; // Logical pixels, all four corners.
    double outlineWidthPx;
    render::Color activeOutline;
    render::Color inactiveOutline;
    render::Color highlight;
    render::Color shellOutline;
    render::Color dockOutline;
    render::Color dockHighlight;
    // Black laid over a window while a modal transient blocks it.
    float dimOpacity;
    render::Color snapPreviewFill;
    render::Color snapPreviewOutline;
    render::Color snapPreviewHighlight;
};

} // namespace cutefish::decoration
