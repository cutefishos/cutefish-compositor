// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_WINDOW_FRAME_H_
#define CUTEFISH_WM_WINDOW_FRAME_H_

#include <memory>
#include <vector>

#include "base/geometry.h"
#include "input/cursor_shape.h"
#include "input/types.h"
#include "wm/types.h"

namespace cutefish::wm {

class Window;

enum class FrameArea {
    None,
    TitleBar,
    CaptionButton,
    ResizeBorder,
};

class WindowFrame
{
public:
    virtual ~WindowFrame() = default;

    // Zero while the window is fullscreen.
    virtual Insets extents() const = 0;

    // In output coordinates, reaching outside the window bounds.
    virtual base::Rect inputBounds() const = 0;

    virtual FrameArea areaAt(const base::PointF& location) const = 0;

    virtual input::CursorShape cursorShapeAt(const base::PointF&) const
    {
        return input::CursorShape::Default;
    }

    // In output coordinates. Each returns true when the frame consumed the event.
    virtual bool onPointerMotion(input::Timestamp timestamp, const base::PointF& location) = 0;
    virtual bool onPointerButton(input::Timestamp timestamp, const base::PointF& location,
                                 bool pressed) = 0;
    virtual void onPointerLeave() = 0;

    virtual bool isTrackingPress() const = 0;
    virtual void cancelPress() { }

    // In output coordinates; empty for a frame that draws none.
    virtual base::Rect titleBarBounds() const { return base::Rect(); }
    virtual std::vector<base::Rect> captionButtonBounds() const { return {}; }

    virtual bool onTouchDown(input::Timestamp, int32_t, const base::PointF&) { return false; }
    virtual bool onTouchMotion(input::Timestamp, int32_t, const base::PointF&) { return false; }
    virtual bool onTouchUp(input::Timestamp, int32_t) { return false; }
    virtual void onTouchCancel() { }
};

// Answers what a frame would cost before there is one, for the initial configure.
class FrameProvider
{
public:
    virtual ~FrameProvider() = default;

    virtual std::unique_ptr<WindowFrame> createFrame(Window* window) = 0;

    virtual Insets defaultExtents() const = 0;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_FRAME_H_
