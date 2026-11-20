// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modelled on chromium/src/ash/wm/wm_event.h (revision
// 7acd40ddefff49c9d36144999aafdbb674dbd07a).

#ifndef CUTEFISH_WM_WM_EVENT_H_
#define CUTEFISH_WM_WM_EVENT_H_

#include "base/geometry.h"

namespace cutefish::wm {

enum class WMEventType {
    // The request may not be granted.
    Normal,
    Maximize,
    Minimize,
    Fullscreen,
    SnapPrimary,
    SnapSecondary,

    Restore,

    // Honoured only in the normal state.
    SetBounds,

    ToggleMaximize,
    ToggleFullscreen,

    WorkAreaChanged,
};

class WMEvent
{
public:
    explicit WMEvent(WMEventType type);
    WMEvent(const WMEvent&) = delete;
    WMEvent& operator=(const WMEvent&) = delete;
    virtual ~WMEvent();

    WMEventType type() const { return m_type; }

private:
    WMEventType m_type;
};

class SetBoundsWMEvent : public WMEvent
{
public:
    explicit SetBoundsWMEvent(const base::Rect& bounds);
    ~SetBoundsWMEvent() override;

    const base::Rect& requestedBounds() const { return m_requestedBounds; }

private:
    base::Rect m_requestedBounds;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WM_EVENT_H_
