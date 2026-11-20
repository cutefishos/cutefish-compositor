// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SCENE_FINGER_TRACK_H_
#define CUTEFISH_SCENE_FINGER_TRACK_H_

#include <deque>
#include <optional>

namespace cutefish::scene::motion {

// Positions are in the caller's unit; velocities are that unit per second.
class FingerTrack
{
public:
    static constexpr double kResampleLatencyMs = 5;
    static constexpr double kResampleMaxPredictionMs = 8;
    static constexpr double kResampleMinDeltaMs = 2;
    static constexpr double kVelocityHorizonMs = 100;
    static constexpr double kAssumeMoveStoppedMs = 40;
    static constexpr double kAssumeReleaseStoppedMs = 80;

    struct Sample {
        double timeMs = 0;
        double position = 0;
    };

    // Events arriving out of order are taken at the newest time seen.
    void add(double timeMs, double position);
    void clear() { m_samples.clear(); }
    bool empty() const { return m_samples.empty(); }

    void shift(double by);

    // Needs at least one event.
    double position(double timeMs) const;

    // 0 when the fingers had stopped before `releaseTimeMs`.
    double releaseVelocity(double releaseTimeMs) const;

    // Empty when the events came too close together to tell.
    std::optional<double> recentVelocity() const;

    double predictionEnd() const;

    bool isCatchingUp(double frameTime) const
    {
        return !m_samples.empty()
                && frameTime - kResampleLatencyMs
                        < m_samples.back().timeMs + kResampleMaxPredictionMs;
    }

private:
    std::deque<Sample> m_samples;
};

} // namespace cutefish::scene::motion

#endif // CUTEFISH_SCENE_FINGER_TRACK_H_
