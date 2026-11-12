// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/drm/gpu_boost_policy.h"

namespace cutefish::backend {

void GpuBoostPolicy::compositorFrame(Time now)
{
    if (!m_active)
        return;
    if (holds(now) || (m_lastFrame && now - *m_lastFrame <= kStreakGap))
        m_holdUntil = now + kLinger;
    m_lastFrame = now;
}

void GpuBoostPolicy::expectAnimation(Time now)
{
    if (m_active)
        m_holdUntil = now + kLinger;
}

void GpuBoostPolicy::setActive(bool active)
{
    m_active = active;
    if (!active) {
        m_lastFrame.reset();
        m_holdUntil.reset();
    }
}

bool GpuBoostPolicy::holds(Time now) const
{
    return m_holdUntil && now < *m_holdUntil;
}

std::optional<GpuBoostPolicy::Time> GpuBoostPolicy::releaseTime() const
{
    return m_holdUntil;
}

} // namespace cutefish::backend
