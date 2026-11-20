// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_RESIZE_HANDLES_H_
#define CUTEFISH_WM_RESIZE_HANDLES_H_

#include "base/geometry.h"
#include "render/rounded_shape.h"
#include "wm/types.h"

namespace cutefish::decoration {
struct WindowVisualStyle;
}

namespace cutefish::wm {

class Window;

constexpr int kResizeOutsideBoundsSize = 6;
constexpr int kResizeOutsideBoundsScaleForTouch = 5;
constexpr int kResizeInsideBoundsSize = 1;
constexpr int kResizeAreaCornerSize = 16;

// The caller bounds the outside.
ResizeEdge resizeEdgeFor(const render::RoundedShape& shape, const base::Rect& frame,
                         const base::Point& point, int inside, int corner);

// `location` is in output coordinates.
ResizeEdge serverResizeEdgeAt(const Window& window, const decoration::WindowVisualStyle& style,
                              const base::PointF& location, bool touch);

} // namespace cutefish::wm

#endif // CUTEFISH_WM_RESIZE_HANDLES_H_
