// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_DOCK_DOCK_MAGNIFICATION_H_
#define CUTEFISH_SHELL_DOCK_DOCK_MAGNIFICATION_H_

namespace cutefish::shell {

class DockMagnification
{
public:
    static constexpr double kRadiusSlots = 3.5;

    DockMagnification() = default;
    // `slot` is a slot's length at rest; `peak` is at least 1.
    DockMagnification(double focus, double amount, double peak, double slot);

    bool isIdentity() const { return m_height == 0; }
    double map(double position) const;
    double expansion() const { return m_height * m_radius; }

private:
    double displacement(double offset) const;

    double m_focus = 0;
    double m_radius = 0;
    double m_height = 0;
};

class DockMagnifier
{
public:
    static constexpr double kAmountRampMs = 140;
    static constexpr double kFocusSettleMs = 120;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    // True when what is drawn changes.
    bool track(double along);
    bool leave();
    bool isTracking() const { return m_tracking; }
    void setHeld(bool held);
    bool isHeld() const { return m_held; }
    void setSuppressed(bool suppressed);
    bool isSuppressed() const { return m_suppressed; }

    // The first frame after stop() is one frame interval on.
    void advance(double nowMs);
    void stop() { m_lastMs = -1; }
    bool isMoving() const;

    double amount() const { return m_progress * m_progress * (3 - 2 * m_progress); }
    double focus() const { return m_held ? m_heldFocus : m_pointer + m_offset; }

private:
    void freezeFocus();
    void updateTarget();

    bool m_enabled = false;
    bool m_tracking = false;
    bool m_held = false;
    bool m_suppressed = false;
    double m_pointer = 0;
    double m_offset = 0;
    double m_offsetVelocity = 0;
    double m_heldFocus = 0;
    double m_progress = 0;
    double m_target = 0;
    double m_lastMs = -1;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_DOCK_DOCK_MAGNIFICATION_H_
