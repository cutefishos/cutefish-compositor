// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/wayland/wayland_positioner.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WAYLAND_PROTOCOLS_XDG_POSITIONER_H_
#define CUTEFISH_WAYLAND_PROTOCOLS_XDG_POSITIONER_H_

#include <cstdint>

#include "base/geometry.h"

namespace cutefish::wayland {

class WaylandPositioner
{
public:
    struct Result {
        base::Point origin;
        base::Size size;

        base::Rect toRect() const { return base::Rect(origin, size); }
    };

    enum Direction { kNegative = -1, kNeutral = 0, kPositive = 1 };

    WaylandPositioner() = default;
    // Copyable, for a popup that keeps its positioner until it has a parent.
    WaylandPositioner(const WaylandPositioner&) = default;
    WaylandPositioner& operator=(const WaylandPositioner&) = default;

    // `workArea` is in the same coordinate space as the anchor rect, which for
    // xdg_positioner is the parent surface's window geometry.
    Result calculateBounds(const base::Rect& workArea) const;
    // The same with the anchor rect moved by `anchorOffset`, for an anchor
    // given in the coordinates of a surface inside the parent.
    Result calculateBounds(const base::Rect& workArea, const base::Point& anchorOffset) const;

    void setSize(const base::Size& size) { m_size = size; }
    void setAnchorRect(const base::Rect& anchorRect) { m_anchorRect = anchorRect; }
    void setAnchor(uint32_t anchor);
    void setGravity(uint32_t gravity);
    void setAdjustment(uint32_t adjustment) { m_adjustment = adjustment; }
    void setOffset(const base::Point& offset) { m_offset = offset; }

    base::Size size() const { return m_size; }

private:
    // -1x-1 until set_size.
    base::Size m_size {-1, -1};
    base::Rect m_anchorRect;

    Direction m_anchorX = kNeutral;
    Direction m_anchorY = kNeutral;

    Direction m_gravityX = kNeutral;
    Direction m_gravityY = kNeutral;

    uint32_t m_adjustment = 0;

    // An absolute translation, unaffected by flipping, sliding or resizing.
    base::Point m_offset;
};

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PROTOCOLS_XDG_POSITIONER_H_
