// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_DOCK_DOCK_MAGNIFICATION_H_
#define CUTEFISH_SHELL_DOCK_DOCK_MAGNIFICATION_H_

namespace cutefish::shell {

// Icons near the pointer grow and push their neighbours apart. Every position
// along the dock as laid out at rest is drawn at map(position): the slot
// centred on the focus comes out `peak` times its length at amount 1, the
// growth falls off as a raised cosine over kRadiusSlots slots to either side,
// and the focus itself stays where it is. Identity at amount 0.
class DockMagnification
{
public:
    static constexpr double kRadiusSlots = 3.5;

    DockMagnification() = default;
    // `slot` is a slot's length at rest, `peak` at least 1.
    DockMagnification(double focus, double amount, double peak, double slot);

    bool isIdentity() const { return m_height == 0; }
    double map(double position) const;
    // How much longer the whole row is drawn when both of its ends are
    // farther than the radius from the focus.
    double expansion() const { return m_height * m_radius; }

private:
    double displacement(double offset) const;

    double m_focus = 0;
    double m_radius = 0;
    // The profile's height above 1 at the focus, times the amount.
    double m_height = 0;
};

// What drives the magnification: the amount, toward 1 while the pointer is
// in the dock and toward 0 otherwise, eased in and out over a ramp stepped on
// frames that turns around where it is; and the focus, the pointer itself,
// which glides from where the dock was magnified as a critically damped
// spring when it picks up a pointer somewhere else. A hold (an
// open menu) freezes both; a suppression (a drag) fades it out in place.
class DockMagnifier
{
public:
    static constexpr double kAmountRampMs = 140;
    static constexpr double kFocusSettleMs = 120;

    // Off: nothing is magnified, at once.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    // The pointer is in the tracking band at `along`, or has left it; true
    // when what is drawn changes.
    bool track(double along);
    bool leave();
    bool isTracking() const { return m_tracking; }
    // Held, the focus and where the amount is heading stay as they are, and
    // the pointer is not tracked; on release it is not tracked until track().
    void setHeld(bool held);
    bool isHeld() const { return m_held; }
    void setSuppressed(bool suppressed);
    bool isSuppressed() const { return m_suppressed; }

    // To the frame at `nowMs`; the first frame after stop() is one frame
    // interval on.
    void advance(double nowMs);
    void stop() { m_lastMs = -1; }
    bool isMoving() const;

    // Smoothstep of the ramp: exactly 0 and 1 at its ends.
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
    // From the pointer to the focus; glides to 0.
    double m_offset = 0;
    double m_offsetVelocity = 0;
    double m_heldFocus = 0;
    // The ramp, at 1 / kAmountRampMs toward the target.
    double m_progress = 0;
    double m_target = 0;
    double m_lastMs = -1;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_DOCK_DOCK_MAGNIFICATION_H_
