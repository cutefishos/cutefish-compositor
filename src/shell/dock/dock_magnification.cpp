// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/dock/dock_magnification.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "shell/dock/dock_presentation.h"

namespace cutefish::shell {

namespace {

// A critically damped spring is within 1 % of its target, from rest, after
// this many time constants: (1 + x) e^-x = 0.01.
constexpr double kSettledTimeConstants = 6.64;
// Closer than this, with as little speed left, it is there.
constexpr double kOffsetSnap = 1e-2;

// The spring toward `target` over `seconds`, solved exactly so any frame
// interval steps it the same way.
void spring(double &value, double &velocity, double target, double settleMs, double seconds, double snap)
{
    const double rate = kSettledTimeConstants / (settleMs / 1000);
    const double error = value - target;
    const double drive = velocity + rate * error;
    const double decay = std::exp(-rate * seconds);
    value = target + (error + drive * seconds) * decay;
    velocity = (velocity - rate * drive * seconds) * decay;
    if (std::abs(value - target) < snap && std::abs(velocity) < snap * rate) {
        value = target;
        velocity = 0;
    }
}

} // namespace

DockMagnification::DockMagnification(double focus, double amount, double peak, double slot)
    : m_focus(focus)
    , m_radius(kRadiusSlots * slot)
{
    if (amount <= 0 || peak <= 1 || slot <= 0)
        return;
    // The slot centred on the focus averages the profile over its length;
    // the height makes that average the peak.
    const double half = std::numbers::pi * slot / (2 * m_radius);
    const double average = (1 + std::sin(half) / half) / 2;
    m_height = std::min(amount, 1.0) * (peak - 1) / average;
}

double DockMagnification::map(double position) const
{
    if (isIdentity())
        return position;
    return position + displacement(position - m_focus);
}

double DockMagnification::displacement(double offset) const
{
    // The integral of the profile above 1, from the focus to `offset`.
    if (offset >= m_radius)
        return m_height * m_radius / 2;
    if (offset <= -m_radius)
        return -m_height * m_radius / 2;
    return m_height
            * (offset / 2 + m_radius / (2 * std::numbers::pi) * std::sin(std::numbers::pi * offset / m_radius));
}

void DockMagnifier::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    if (enabled)
        return;
    m_tracking = false;
    m_held = false;
    m_suppressed = false;
    m_offset = m_offsetVelocity = 0;
    m_progress = m_target = 0;
    m_lastMs = -1;
}

bool DockMagnifier::track(double along)
{
    if (!m_enabled || m_held || m_suppressed)
        return false;
    if (m_tracking && along == m_pointer)
        return false;
    if (!m_tracking) {
        // Still drawn magnified somewhere: the focus glides from there.
        m_offset = amount() > 0 ? focus() - along : 0;
        m_offsetVelocity = 0;
        m_tracking = true;
    }
    m_pointer = along;
    updateTarget();
    return true;
}

bool DockMagnifier::leave()
{
    if (!m_tracking || m_held || m_suppressed)
        return false;
    m_tracking = false;
    freezeFocus();
    updateTarget();
    return true;
}

void DockMagnifier::setHeld(bool held)
{
    if (held == m_held || !m_enabled)
        return;
    if (held) {
        m_heldFocus = focus();
        m_held = true;
        return;
    }
    m_held = false;
    m_tracking = false;
    m_pointer = m_heldFocus;
    m_offset = m_offsetVelocity = 0;
    updateTarget();
}

void DockMagnifier::setSuppressed(bool suppressed)
{
    if (suppressed == m_suppressed || !m_enabled)
        return;
    m_suppressed = suppressed;
    if (suppressed) {
        m_tracking = false;
        freezeFocus();
    }
    updateTarget();
}

void DockMagnifier::freezeFocus()
{
    m_pointer = focus();
    m_offset = m_offsetVelocity = 0;
}

void DockMagnifier::updateTarget()
{
    if (!m_held)
        m_target = m_enabled && m_tracking && !m_suppressed ? 1 : 0;
}

void DockMagnifier::advance(double nowMs)
{
    // At most a frame interval a frame: a frame drawn at once early in an idle
    // refresh is stamped when it was drawn, ahead of its slot, so the next
    // stamp is up to two intervals on although the two show a refresh apart.
    const double elapsedMs = m_lastMs < 0 ? DockPresentation::kFrameIntervalMs
                                          : std::clamp(nowMs - m_lastMs, 0.0, DockPresentation::kFrameIntervalMs);
    m_lastMs = nowMs;
    const double step = elapsedMs / kAmountRampMs;
    m_progress = m_target > m_progress ? std::min(m_target, m_progress + step) : std::max(m_target, m_progress - step);
    if (!m_held)
        spring(m_offset, m_offsetVelocity, 0, kFocusSettleMs, elapsedMs / 1000, kOffsetSnap);
}

bool DockMagnifier::isMoving() const
{
    return m_progress != m_target || (!m_held && (m_offset != 0 || m_offsetVelocity != 0));
}

} // namespace cutefish::shell
