// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_OVERVIEW_OVERVIEW_LAYOUT_H_
#define CUTEFISH_WM_OVERVIEW_OVERVIEW_LAYOUT_H_

#include "base/geometry.h"

#include <vector>

namespace cutefish::wm::overview {

// `gap` is in logical pixels; rectangles land on whole pixels at `deviceScale`.
// The same windows in the same order always give the same arrangement.
std::vector<base::RectF> spreadLayout(const std::vector<base::Rect> &windows, const base::Rect &area, int gap,
                                 double deviceScale);

} // namespace cutefish::wm::overview

#endif // CUTEFISH_WM_OVERVIEW_OVERVIEW_LAYOUT_H_
