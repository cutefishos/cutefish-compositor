// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The three window controls in the titlebar.
//
// Behaviour follows ash/frame/caption_buttons: a button tracks its own hover
// and pressed state, a press is only honoured if the release lands on the same
// button, and the action is a window-state request rather than anything done to
// the client directly. The artwork is the toolkit's window controls, so
// frames drawn here match the headers apps draw.

#ifndef CUTEFISH_DECORATION_CAPTION_BUTTON_H_
#define CUTEFISH_DECORATION_CAPTION_BUTTON_H_

#include "base/geometry.h"

namespace cutefish::decoration {

enum class CaptionButtonType {
    Close,
    Minimize,
    Maximize,
};

class CaptionButton
{
public:
    explicit CaptionButton(CaptionButtonType type);

    CaptionButtonType type() const { return m_type; }

    base::Rect geometry() const { return m_geometry; }
    void setGeometry(const base::Rect& geometry) { m_geometry = geometry; }

    bool isHovered() const { return m_hovered; }
    // Returns true when the state changed, so the caller can repaint.
    bool setHovered(bool hovered);

    bool isPressed() const { return m_pressed; }
    bool setPressed(bool pressed);

private:
    CaptionButtonType m_type;
    base::Rect m_geometry;
    bool m_hovered = false;
    bool m_pressed = false;
};

} // namespace cutefish::decoration

#endif // CUTEFISH_DECORATION_CAPTION_BUTTON_H_
