// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_WORK_AREA_H_
#define CUTEFISH_WM_WORK_AREA_H_

#include <map>
#include <string>

#include "base/geometry.h"
#include "wm/types.h"

namespace cutefish::wm {

// Keys are opaque tokens owned by the caller.
class ExclusiveZones
{
public:
    struct Zone {
        ScreenEdge edge = ScreenEdge::Top;
        int thickness = 0;
        std::string outputName;
        bool yieldsToMaximized = false;
    };

    // Returns true when the work area has to be recomputed.
    bool set(const void* token, ScreenEdge edge, int thickness,
             const std::string& outputName = {}, bool yieldsToMaximized = false);
    bool clear(const void* token);

    bool isEmpty() const { return m_zones.empty(); }
    bool hasYieldingZone() const;

    // Reservations on the same edge do not stack: the largest wins.
    base::Rect apply(const base::Rect& outputBounds) const;
    base::Rect apply(const base::Rect& outputBounds, const std::string& outputName, bool primary,
                     bool maximized = false) const;

private:
    std::map<const void*, Zone> m_zones;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WORK_AREA_H_
