// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/system/night_light/night_light_controller_impl.cc
//               (ColorTemperatureAnimation, MatrixFromTemperature,
//                BlueColorScaleFromTemperature, GreenColorScaleFromTemperature)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#pragma once

#include <chrono>
#include <functional>

#include "base/timer.h"
#include "render/color_transform.h"

namespace cutefish::display {

// Strength 0 leaves the screen untouched; 1 is the warmest.
class NightLight
{
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    using Apply = std::function<void(const render::ColorTransform &)>;

    explicit NightLight(Apply apply, Clock clock = std::chrono::steady_clock::now);
    NightLight(const NightLight &) = delete;
    NightLight &operator=(const NightLight &) = delete;

    void animateTo(double strength, std::chrono::milliseconds duration);

    double strength() const { return m_current; }

    double target() const { return m_target; }

    bool isAnimating() const { return m_timer.isActive(); }

    // Red stays; blue is toned down, and green half as much so the screen does
    // not turn green.
    static render::ColorTransform transformFor(double strength);

    void stepForTesting() { step(); }

private:
    void step();

    Apply m_apply;
    Clock m_clock;
    base::Timer m_timer;
    double m_start = 0;
    double m_current = 0;
    double m_target = 0;
    std::chrono::steady_clock::time_point m_startTime;
    std::chrono::milliseconds m_duration {0};
};

} // namespace cutefish::display
