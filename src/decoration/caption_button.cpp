// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decoration/caption_button.h"

namespace cutefish::decoration {

CaptionButton::CaptionButton(CaptionButtonType type)
    : m_type(type)
{
}

bool CaptionButton::setHovered(bool hovered)
{
    if (hovered == m_hovered)
        return false;
    m_hovered = hovered;
    return true;
}

bool CaptionButton::setPressed(bool pressed)
{
    if (pressed == m_pressed)
        return false;
    m_pressed = pressed;
    return true;
}

} // namespace cutefish::decoration
