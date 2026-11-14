// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_METRICS_H_
#define CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_METRICS_H_

#include "base/geometry.h"

namespace cutefish::shell {

// The launchpad's layout: every metric a
// fraction of the surface, clamped at both ends, in logical pixels.
struct LaunchpadMetrics {
    // Room the launchpad leaves for other panels: the dock on its edge and a
    // constant 8 at the top.
    struct Margins {
        int top = 8;
        int left = 0;
        int right = 0;
        int bottom = 0;

        bool operator==(const Margins &) const = default;
    };

    static LaunchpadMetrics compute(const base::Size &surface, const Margins &margins);

    int width = 0;
    int height = 0;
    Margins margins;

    int outerMarginH = 0;
    int outerMarginV = 0;
    int sectionSpacing = 0;
    int searchPixelSize = 0;
    int searchFieldWidth = 0;
    int searchFieldHeight = 0;
    int searchIconSize = 0;
    int searchIconGap = 6;
    int indicatorDotSize = 0;
    int notFoundPixelSize = 0;

    // The content box: the search row at its top, the indicator row at its
    // bottom, the grid area between them.
    base::Rect content;
    base::Rect searchRow;
    base::Rect gridArea;
    base::Rect indicatorRow;

    int columns = 1;
    int rows = 1;
    // Icons per page, never zero.
    int pageCount = 1;
    static constexpr double kContentWidthRatio = 0.86;
    int cellWidth = 64;
    int cellHeight = 64;
    int labelPixelSize = 11;
    int labelLineHeight = 14;
    int iconSize = 32;
    int cellSpacing = 6;
    int cellPaddingV = 0;
    int selectionWidth = 0;
    int selectionPadding = 0;

    // The grid of a page, centred in the grid area.
    base::Rect pageGrid() const;
    // The cell of an index within its page, in surface coordinates.
    base::Rect cellRect(int indexInPage) const;
    int pagesFor(int count) const { return count > 0 ? (count + pageCount - 1) / pageCount : 0; }

    bool operator==(const LaunchpadMetrics &) const = default;
};

enum class LaunchpadDirection {
    Left,
    Right,
    Up,
    Down,
};

// AllAppsView.moveSelection(): the selection after an arrow key, or -1 to
// leave it. `selected` is an index into the whole list (-1 for none), `page`
// the page shown.
int moveLaunchpadSelection(int selected, int page, int total, int columns, int pageCount,
                           LaunchpadDirection direction);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_METRICS_H_
