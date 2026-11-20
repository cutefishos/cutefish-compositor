// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_MOTION_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_MOTION_H_

#include <chrono>
#include <cstdint>
#include <functional>

#include "ui/frame_client.h"

namespace cutefish::shell {

inline constexpr std::chrono::milliseconds kMotionDuration {380};
double motionEasing(double progress);
double outCubic(double progress);

class Tween : public ui::FrameClient
{
public:
    using Easing = double (*)(double);

    explicit Tween(ui::Window *window, std::chrono::milliseconds duration = kMotionDuration,
                   Easing easing = motionEasing);

    double value() const { return m_value; }
    double target() const { return m_to; }
    void setDuration(std::chrono::milliseconds duration) { m_duration = duration; }

    void setTarget(double target, bool animate);
    void snap(double value);

    std::function<void()> onChanged;
    std::function<void()> onFinished;

protected:
    void tick(uint64_t frameTimeNs) override;

private:
    std::chrono::milliseconds m_duration;
    Easing m_easing;
    double m_value = 0;
    double m_from = 0;
    double m_to = 0;
    uint64_t m_startNs = 0;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_MOTION_H_
