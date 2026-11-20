// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_TYPES_H_
#define CUTEFISH_WM_TYPES_H_

#include <cstdint>

#include "base/geometry.h"

namespace cutefish::wm {

enum class WindowType {
    Normal,
    Popup,
};

enum class WindowStateType {
    Normal,
    PrimarySnapped,
    SecondarySnapped,
    Maximized,
    Fullscreen,
    Minimized,
};

constexpr bool isSnappedStateType(WindowStateType type)
{
    return type == WindowStateType::PrimarySnapped || type == WindowStateType::SecondarySnapped;
}

constexpr bool isRoundedStateType(WindowStateType type)
{
    return type == WindowStateType::Normal || type == WindowStateType::Maximized
            || isSnappedStateType(type);
}

enum class WindowRole {
    Normal,
    Desktop,
    Panel,
    Dock,
    Launchpad,
    Notification,
    // The screen capture interface; never painted into a capture.
    Capture,
    // A volume or brightness display; never focusable.
    Osd,
    // A shell dialog hanging from an application window, window-modal for it.
    Sheet,
    // A chooser, such as the clipboard history; only borrows the keyboard.
    Picker,
};

// A Panel or Dock still receives pointer input; only activation is refused.
enum class FocusPolicy {
    Normal,
    NoFocus,
};

// Ignored for other roles.
enum class PanelBehavior {
    StayAbove,
    WindowsCanCover,
};

enum class ScreenEdge {
    Top,
    Bottom,
    Left,
    Right,
};

enum class ResizeEdge {
    None,
    Top,
    Bottom,
    Left,
    Right,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

constexpr bool isLeftEdge(ResizeEdge edge)
{
    return edge == ResizeEdge::Left || edge == ResizeEdge::TopLeft
        || edge == ResizeEdge::BottomLeft;
}

constexpr bool isRightEdge(ResizeEdge edge)
{
    return edge == ResizeEdge::Right || edge == ResizeEdge::TopRight
        || edge == ResizeEdge::BottomRight;
}

constexpr bool isTopEdge(ResizeEdge edge)
{
    return edge == ResizeEdge::Top || edge == ResizeEdge::TopLeft
        || edge == ResizeEdge::TopRight;
}

constexpr bool isBottomEdge(ResizeEdge edge)
{
    return edge == ResizeEdge::Bottom || edge == ResizeEdge::BottomLeft
        || edge == ResizeEdge::BottomRight;
}

constexpr bool changesWidth(ResizeEdge edge)
{
    return isLeftEdge(edge) || isRightEdge(edge);
}

constexpr bool changesHeight(ResizeEdge edge)
{
    return isTopEdge(edge) || isBottomEdge(edge);
}

// All zero for a client-decorated or fullscreen window.
struct Insets {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    bool isNull() const { return left == 0 && top == 0 && right == 0 && bottom == 0; }

    // Outer rect -> client area.
    base::Rect inset(const base::Rect& rect) const
    {
        return rect.adjusted(left, top, -right, -bottom);
    }

    // Client area -> outer rect.
    base::Rect outset(const base::Rect& rect) const
    {
        return rect.adjusted(-left, -top, right, bottom);
    }

    base::Size outset(const base::Size& size) const
    {
        return base::Size(size.width() + left + right, size.height() + top + bottom);
    }

    base::Size inset(const base::Size& size) const
    {
        return base::Size(size.width() - left - right, size.height() - top - bottom);
    }

    base::Point topLeft() const { return base::Point(left, top); }

    friend bool operator==(const Insets& a, const Insets& b)
    {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
    }
};

// The one above and left of the middle for an even size.
inline base::Point centerPixel(const base::Rect& rect)
{
    return base::Point(int((int64_t(rect.x()) * 2 + rect.width() - 1) / 2),
                       int((int64_t(rect.y()) * 2 + rect.height() - 1) / 2));
}

const char* toString(WindowStateType type);
const char* toString(WindowRole role);
const char* toString(ResizeEdge edge);

} // namespace cutefish::wm

#endif // CUTEFISH_WM_TYPES_H_
