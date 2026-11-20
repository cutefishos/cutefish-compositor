// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/gestures/wm_gesture_handler.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_WM_GESTURES_WM_GESTURE_HANDLER_H_
#define CUTEFISH_WM_GESTURES_WM_GESTURE_HANDLER_H_

#include <functional>

#include "input/gesture_filter.h"

namespace cutefish::wm {

class DesksController;
class OverviewController;

class WmGestureHandler : public input::GestureFilter
{
public:
    static constexpr uint32_t kDeskSwitchFingers = 3;

    static constexpr double kContinuousGestureMoveThresholdMm = 2;

    static constexpr double kCatchUpTravelRatio = 2;

    static constexpr double kSwipeLengthPerDeskMm = 80;

    static constexpr double kOverviewSwipeLengthMm = 50;

    // libinput's unaccelerated touchpad unit is a thousandth of an inch scaled by 0.9 * 0.2968.
    static constexpr double kMmPerUnacceleratedUnit = 25.4 / 1000 / (0.9 * 0.2968);

    // An empty `dragging` means never. Without `overview`, vertical swipes do nothing.
    explicit WmGestureHandler(DesksController *desks, std::function<bool()> dragging = {},
                              OverviewController *overview = nullptr);

    // Runs before the swipe moves anything, so its first frame does not meet an idle GPU.
    void setWarmUp(std::function<void()> warmUp) { m_warmUp = std::move(warmUp); }

    bool claimGesture(input::Timestamp timestamp, input::GestureType type,
                      uint32_t fingers) override;
    void onGestureUpdate(input::Timestamp timestamp, const input::GestureUpdate &update) override;
    void onGestureEnd(input::Timestamp timestamp, bool cancelled) override;

private:
    double catchUp(double delta);

    struct ScrollData {
        // In millimetres.
        double scrollX = 0;
        double scrollY = 0;
        bool horizontalContinuousGestureStarted = false;
        bool verticalContinuousGestureStarted = false;
        double owed = 0;
        double owedTravel = 0;
        double timeMs = 0;
        bool ignored = false;
    };

    DesksController *m_desks = nullptr;
    std::function<bool()> m_dragging;
    OverviewController *m_overview = nullptr;
    std::function<void()> m_warmUp;
    ScrollData m_scroll;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_GESTURES_WM_GESTURE_HANDLER_H_
