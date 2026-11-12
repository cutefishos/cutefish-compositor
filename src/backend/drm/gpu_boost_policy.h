// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_BACKEND_DRM_GPU_BOOST_POLICY_H_
#define CUTEFISH_BACKEND_DRM_GPU_BOOST_POLICY_H_

#include <chrono>
#include <optional>

namespace cutefish::backend {

// When the GPU clock is held up for what the compositor draws itself: from the
// second of two compositor frames that come within kStreakGap of each other,
// as animations, drags and gestures make them, until kLinger after the last
// one; never while the session is inactive. A one-off redraw, such as a clock
// or a text cursor, holds nothing, and a client presenting frames is not
// counted at all.
class GpuBoostPolicy
{
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;

    // Wide enough for frames that each take three refreshes at a throttled
    // clock, narrow enough to leave a text cursor's blinks apart.
    static constexpr Clock::duration kStreakGap = std::chrono::milliseconds(100);
    // Bridges the gaps between frames at a throttled clock and between
    // animations that follow one another, without holding through steady state.
    static constexpr Clock::duration kLinger = std::chrono::milliseconds(500);

    // A frame showing a change the compositor made was drawn at `now`.
    void compositorFrame(Time now);
    // Holds at once: what starts at `now`, a gesture, is known to animate.
    void expectAnimation(Time now);

    void setActive(bool active);

    bool holds(Time now) const;

    // When holds() turns false unless more frames come; empty before a hold
    // and while inactive.
    std::optional<Time> releaseTime() const;

private:
    bool m_active = true;
    std::optional<Time> m_lastFrame;
    std::optional<Time> m_holdUntil;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_GPU_BOOST_POLICY_H_
