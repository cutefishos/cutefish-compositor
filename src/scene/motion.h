// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SCENE_MOTION_H_
#define CUTEFISH_SCENE_MOTION_H_

namespace cutefish::scene::motion {

// `response` is the undamped period in seconds; dampingRatio 1 is critically damped.
struct SpringParameters {
    double response = 0.35;
    double dampingRatio = 1;
};

// 1 unless --animation-duration-scale sets a positive number.
double durationScale();
void setDurationScale(double scale);

// `x` is clamped to [0, 1].
double cubicBezier(double x1, double y1, double x2, double y2, double x);

// Times are milliseconds on the frame clock; velocities are units per second.
class Spring
{
public:
    Spring();
    explicit Spring(const SpringParameters &parameters);

    void reset(double time, double value, double target, double velocity = 0);

    void retarget(double time, double target);

    double value(double time) const;
    double velocity(double time) const;

    double target() const { return m_target; }

private:
    // `elapsed` is in seconds after m_start.
    double displacement(double elapsed) const;
    double displacementVelocity(double elapsed) const;
    double elapsed(double time) const;
    void updateCoefficients();

    SpringParameters m_parameters;
    double m_omega = 0;
    double m_zeta = 1;
    double m_start = 0;
    double m_displacement = 0;
    double m_velocity = 0;
    double m_target = 0;
};

struct TimingCurve {
    double x1;
    double y1;
    double x2;
    double y2;
};
inline constexpr TimingCurve kAccel20Decel100 {0.2, 0, 0, 1};
inline constexpr TimingCurve kQuickStart {0.2, 0, 0.4, 1};

class Tween
{
public:
    Tween() = default;
    // `velocity` is in units per second.
    Tween(double time, double from, double target, double durationMs, double velocity = 0,
          const TimingCurve &curve = kAccel20Decel100);

    double value(double time) const;
    double velocity(double time) const;

    double target() const { return m_target; }
    bool isFinished(double time) const { return time >= m_start + m_duration; }

private:
    double m_start = 0;
    double m_from = 0;
    double m_target = 0;
    double m_duration = 0;
    double m_x1 = kAccel20Decel100.x1;
    double m_y1 = kAccel20Decel100.y1;
    double m_x2 = kAccel20Decel100.x2;
    double m_y2 = kAccel20Decel100.y2;
};

Spring settlingSpring(double time, double from, double target, double velocity, double response);

} // namespace cutefish::scene::motion

#endif // CUTEFISH_SCENE_MOTION_H_
