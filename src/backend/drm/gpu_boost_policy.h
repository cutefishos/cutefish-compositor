// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_BACKEND_DRM_GPU_BOOST_POLICY_H_
#define CUTEFISH_BACKEND_DRM_GPU_BOOST_POLICY_H_

#include <chrono>
#include <optional>

namespace cutefish::backend {

// Holds the clock from the second of two compositor frames within kStreakGap
// until kLinger after the last, never while inactive; client frames do not
// count.
class GpuBoostPolicy
{
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;

    static constexpr Clock::duration kStreakGap = std::chrono::milliseconds(100);
    static constexpr Clock::duration kLinger = std::chrono::milliseconds(500);

    void compositorFrame(Time now);
    void expectAnimation(Time now);

    void setActive(bool active);

    bool holds(Time now) const;

    // Empty before a hold and while inactive.
    std::optional<Time> releaseTime() const;

private:
    bool m_active = true;
    std::optional<Time> m_lastFrame;
    std::optional<Time> m_holdUntil;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_GPU_BOOST_POLICY_H_
