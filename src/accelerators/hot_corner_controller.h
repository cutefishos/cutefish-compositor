// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <functional>
#include <vector>

#include "accelerators/hot_corner_settings.h"
#include "base/geometry.h"

namespace cutefish::input {
class Seat;
} // namespace cutefish::input

namespace cutefish::accelerators {

// Only the outer corners of the whole layout count. A corner fires once per visit.
class HotCornerController
{
public:
    // In logical pixels on either axis.
    static constexpr int kRearmDistance = 8;

    using Perform = std::function<bool(AcceleratorAction)>;

    // `seat` may be null.
    HotCornerController(input::Seat *seat, Perform perform);

    void setOutputs(const std::vector<base::Rect> &outputs);
    void setBinding(HotCorner corner, const HotCornerBinding &binding);

    // After clamping to the layout. A drag uses the visit up without running the action.
    void onPointerMoved(const base::PointF &position, bool dragging = false);

    std::vector<std::pair<HotCorner, base::Point>> corners() const;

    uint32_t heldModifiers() const;

private:
    struct Corner {
        HotCorner corner;
        base::Point point;
        bool armed = true;
    };

    input::Seat *m_seat = nullptr;
    Perform m_perform;
    std::vector<Corner> m_corners;
    std::array<HotCornerBinding, 4> m_bindings;
    base::PointF m_lastPosition {-1e9, -1e9};
};

} // namespace cutefish::accelerators
