// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_DOCK_DOCK_METRICS_H_
#define CUTEFISH_SHELL_DOCK_DOCK_METRICS_H_

#include <algorithm>
#include <cmath>

namespace cutefish::shell {

struct DockMetrics {
    static constexpr int kBaseIconSize = 54;

    int iconSize = 54;
    int iconGap = 16;
    int lengthPadding = 16;
    int outerPadding = 16;
    int dotGap = 8;
    int dotSize = 4;
    int edgePadding = 4;

    static DockMetrics forIconSize(int iconSize)
    {
        const double scale = std::max(1, iconSize) / double(kBaseIconSize);
        // Halves round away from zero.
        const auto scaled = [scale](int value) {
            return std::max(1, int(std::floor(value * scale + 0.5)));
        };
        DockMetrics metrics;
        metrics.iconSize = std::max(1, iconSize);
        metrics.iconGap = scaled(16);
        metrics.lengthPadding = scaled(16);
        metrics.outerPadding = scaled(16);
        metrics.dotGap = scaled(8);
        metrics.dotSize = scaled(4);
        metrics.edgePadding = scaled(4);
        return metrics;
    }

    int thickness() const { return edgePadding + dotSize + dotGap + iconSize + outerPadding; }
    int sectionExtent() const { return int(std::floor(iconGap * 1.5 + 0.5)); }
    int pitch() const { return iconSize + iconGap; }
    int dotAreaSize() const { return edgePadding + dotSize + dotGap; }
    int bounceHeight() const { return int(std::floor(iconSize * 0.35 + 0.5)); }
    int bounceRoom() const { return std::max(0, bounceHeight() - outerPadding); }
    static constexpr double kEndRoomSlots = 0.8;
    int endRoom() const { return int(std::ceil(pitch() * kEndRoomSlots)); }
    int length(int itemCount) const
    {
        return lengthPadding * 2 + itemCount * iconSize + std::max(0, itemCount - 1) * iconGap;
    }

    bool operator==(const DockMetrics &) const = default;
};

struct DrawnDockMetrics {
    double iconSize = 54;
    double iconGap = 16;
    double lengthPadding = 16;
    double outerPadding = 16;
    double dotGap = 8;
    double dotSize = 4;
    double edgePadding = 4;
    double separatorExtent = 24;
    double thickness = 86;

    static DrawnDockMetrics of(const DockMetrics &metrics)
    {
        return {double(metrics.iconSize),      double(metrics.iconGap),
                double(metrics.lengthPadding), double(metrics.outerPadding),
                double(metrics.dotGap),        double(metrics.dotSize),
                double(metrics.edgePadding),   double(metrics.sectionExtent()),
                double(metrics.thickness())};
    }

    static DrawnDockMetrics blend(const DrawnDockMetrics &from, const DrawnDockMetrics &to,
                                  double progress)
    {
        const auto mix = [progress](double a, double b) { return a + (b - a) * progress; };
        return {mix(from.iconSize, to.iconSize),
                mix(from.iconGap, to.iconGap),
                mix(from.lengthPadding, to.lengthPadding),
                mix(from.outerPadding, to.outerPadding),
                mix(from.dotGap, to.dotGap),
                mix(from.dotSize, to.dotSize),
                mix(from.edgePadding, to.edgePadding),
                mix(from.separatorExtent, to.separatorExtent),
                mix(from.thickness, to.thickness)};
    }
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_DOCK_DOCK_METRICS_H_
