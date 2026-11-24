// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <algorithm>
#include <cmath>
#include "base/geometry.h"


namespace cutefish::render {

// decoration::ThemePreferences keeps it in step with the Appearance setting.
bool continuousCorners();
void setContinuousCorners(bool continuous);

// Cubic superellipse corners meet straight edges with zero curvature.
inline double continuousCornerExtent(double radius, double halfSize)
{
    // Keep the extent and exponent in sync with shaders/rounded_clip.glsl.
    return std::clamp(radius * 1.5, double(0), std::max(halfSize, double(0)));
}

inline double cornerExtent(double radius, double halfSize)
{
    return continuousCorners() ? continuousCornerExtent(radius, halfSize)
                               : std::clamp(radius, double(0), std::max(halfSize, double(0)));
}

// In output coordinates.
struct RoundedShape {
    base::RectF bounds;
    double radius = 0;
    base::SizeF radiusScale {1, 1};
    bool circular = false;

    bool contains(const base::PointF& point) const
    {
        // Inclusive of the right and bottom edges, unlike RectF::contains().
        if (bounds.isEmpty() || point.x() < bounds.left() || point.x() > bounds.right() || point.y() < bounds.top()
            || point.y() > bounds.bottom())
            return false;
        const bool continuous = !circular && continuousCorners();
        const double rx = continuous ? continuousCornerExtent(radius * radiusScale.width(), bounds.width() / 2)
                                     : std::clamp(radius * radiusScale.width(), double(0),
                                                  std::max(bounds.width() / 2, double(0)));
        const double ry = continuous ? continuousCornerExtent(radius * radiusScale.height(), bounds.height() / 2)
                                     : std::clamp(radius * radiusScale.height(), double(0),
                                                  std::max(bounds.height() / 2, double(0)));
        if (rx <= 0 || ry <= 0)
            return true;
        const base::PointF d = point - bounds.center();
        const double x = std::max(std::abs(d.x()) - bounds.width() / 2 + rx, double(0)) / rx;
        const double y = std::max(std::abs(d.y()) - bounds.height() / 2 + ry, double(0)) / ry;
        if (!continuous)
            return x * x + y * y <= 1;
        return x * x * x + y * y * y <= 1;
    }

    // Signed distance to the outline, negative inside; roundedDistance() in
    // shaders/rounded_clip.glsl.
    double distance(const base::PointF &point) const
    {
        const bool continuous = !circular && continuousCorners();
        const base::PointF half(bounds.width() / 2, bounds.height() / 2);
        const double rx = continuous ? continuousCornerExtent(radius * radiusScale.width(), half.x())
                                     : std::clamp(radius * radiusScale.width(), double(0),
                                                  std::max(half.x(), double(0)));
        const double ry = continuous ? continuousCornerExtent(radius * radiusScale.height(), half.y())
                                     : std::clamp(radius * radiusScale.height(), double(0),
                                                  std::max(half.y(), double(0)));
        const base::PointF d = point - bounds.center();
        const double ex = std::abs(d.x()) - half.x();
        const double ey = std::abs(d.y()) - half.y();
        if (rx <= 0 || ry <= 0 || ex + rx <= 0 || ey + ry <= 0)
            return std::max(ex, ey);
        const double qx = (ex + rx) / rx;
        const double qy = (ey + ry) / ry;
        if (!continuous) {
            const double l = std::hypot(qx, qy);
            return (l - 1) * l / std::hypot(qx / rx, qy / ry);
        }
        const double l = std::cbrt(qx * qx * qx + qy * qy * qy);
        return (l - 1) * l * l / std::hypot(qx * qx / rx, qy * qy / ry);
    }

    friend bool operator==(const RoundedShape &, const RoundedShape &) = default;
};

} // namespace cutefish::render
