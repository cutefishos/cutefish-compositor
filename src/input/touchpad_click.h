// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_INPUT_TOUCHPAD_CLICK_H_
#define CUTEFISH_INPUT_TOUCHPAD_CLICK_H_

#include "base/geometry.h"
#include "input/types.h"

namespace cutefish::input {

// A finger on a touchpad, in millimetres from the pad's top-left corner.
struct TouchpadContact {
    base::PointF origin;
    base::PointF position;
    Timestamp downTime;
};

// Fingers within an ellipse 50 mm wide and 45 mm tall are together, unless one rested in the
// bottom 10 mm before the other came down.
bool isTwoFingerClick(const TouchpadContact &first, const TouchpadContact &second,
                      Timestamp pressTime, const base::SizeF &padSize);

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_TOUCHPAD_CLICK_H_
