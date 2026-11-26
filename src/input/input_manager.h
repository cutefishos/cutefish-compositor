// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/display/extended_mouse_warp_controller.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_INPUT_MANAGER_H_
#define CUTEFISH_INPUT_INPUT_MANAGER_H_

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>


#include "input/gesture_filter.h"
#include "base/geometry.h"
#include "base/signal.h"
#include "base/weak_ptr.h"
#include "input/pointer_gesture_delegate.h"
#include "input/seat_observer.h"
#include "input/types.h"

namespace cutefish::display {
class DisplayManager;
}

namespace cutefish::scene {
class Node;
}

namespace cutefish::input {

class EventSink;
struct RelativeMotion;
class Seat;

class InputManager : public SeatObserver
{

public:
    explicit InputManager(Seat *seat);
    ~InputManager() override;

    Seat *seat() const { return m_seat; }

    void setDisplayManager(display::DisplayManager *manager) { m_displayManager = manager; }

    // In output coordinates; empty means unbounded.
    void setBounds(const base::Size &bounds);
    // `primary` is where the pointer starts, at its centre, on the first topology with displays.
    void setTopology(const std::vector<base::Rect> &displays, const base::Rect &primary = {});

    const std::vector<base::Rect> &topology() const { return m_topology; }

    base::Size bounds() const { return m_bounds; }

    void setDeviceScale(double scale) { m_deviceScale = scale; }

    void setDeviceBounds(const base::Size &bounds) { m_deviceBounds = bounds; }

    base::Size deviceBounds() const { return m_deviceBounds.isEmpty() ? m_bounds : m_deviceBounds; }

    // Sees every pointer and key event before the seat; may be null.
    void setEventSink(EventSink *sink) { m_eventSink = sink; }

    EventSink *eventSink() const { return m_eventSink; }

    void setGestureFilter(GestureFilter *filter) { m_gestureFilter = filter; }

    // Takes Control+Alt+F1..F12 by their physical keys, whatever the
    // convention, lock or inhibitor, and passes the terminal's number.
    void setVtSwitchHandler(std::function<void(int)> handler) { m_vtSwitch = std::move(handler); }

    base::PointF pointerPosition() const { return m_pointerPosition; }

    // Whether a pointer button is down, whoever took its press: a window
    // move consumes it before the seat sees it.
    bool isPointerButtonHeld() const { return !m_heldButtons.empty(); }

    // Places the pointer without generating an event.
    void warpPointer(const base::PointF &position);

    // Re-targets the still pointer once control returns to the event loop.
    void synthesizeMotion();

    // `hotspot` is relative to the node's own origin.
    void setFallbackCursor(scene::Node *node, const base::Point &hotspot);

    // `delta` is in output pixels; `unaccelerated` is the device's delta before
    // acceleration, which only relative-pointer clients see.
    void onPointerMotion(Timestamp timestamp, const base::PointF &delta, const base::PointF &unaccelerated);

    void onPointerMotion(Timestamp timestamp, const base::PointF &delta)
    {
        onPointerMotion(timestamp, delta, delta);
    }
    void onPointerMotionAbsolute(Timestamp timestamp, const base::PointF &position);
    // Normalized absolute coordinates map to the current logical desktop extent.
    void onPointerMotionNormalized(Timestamp timestamp, const base::PointF &position);
    // A point already in global logical coordinates, from a source other than
    // a device (computer control).
    void onPointerMotionLogical(Timestamp timestamp, const base::PointF &position);
    void onPointerButton(Timestamp timestamp, uint32_t evdevButton, bool pressed);
    void onPointerAxis(Timestamp timestamp, const base::PointF &offset, bool discrete);
    void onPointerAxisStop(Timestamp timestamp);
    // Touchpad gestures. `delta` is in output pixels, like motion.
    void onPointerGestureBegin(Timestamp timestamp, GestureType type, uint32_t fingers);
    void onPointerGestureUpdate(Timestamp timestamp, GestureType type, const base::PointF &delta,
                                const base::PointF &unacceleratedDelta, double scale, double rotation);
    void onPointerGestureEnd(Timestamp timestamp, GestureType type, bool cancelled);
    // `layout` says where the keyboard has its Command key; under the Command convention a PC keyboard's Alt and Meta trade places.
    void onKey(Timestamp timestamp, uint32_t evdevCode, bool pressed,
               KeyboardModifierLayout layout = KeyboardModifierLayout::Pc);

    // `normalized` is 0..1 across the panel in its own orientation. A frame closes each batch of points.
    void onTouchDown(Timestamp timestamp, int32_t id, const base::PointF &normalized);
    void onTouchMotion(Timestamp timestamp, int32_t id, const base::PointF &normalized);
    void onTouchUp(Timestamp timestamp, int32_t id);
    void onTouchCancel(Timestamp timestamp);
    void onTouchFrame(Timestamp timestamp);

    base::Signal<> userActivity;
    // Every pointer move while the desktop has the input, clamped to the
    // layout, before any sink, drag or grab sees it.
    base::Signal<base::PointF> pointerMoved;

private:
    void synthesizePendingMotion();

    void onSurfaceFocused(compositor::Surface *, compositor::Surface *) override { }

    void onCursorChanged() override { updateFallbackCursor(); }

    base::PointF clamp(const base::PointF &position) const;

    uint32_t positionalKey(uint32_t evdevCode, KeyboardModifierLayout layout) const;

    int vtForKey(uint32_t physicalCode) const;

    void dispatchMotion(Timestamp timestamp, const RelativeMotion *relative = nullptr);

    void movePointer(Timestamp timestamp, base::PointF target, const RelativeMotion &relative);
    void moveAbsolute(Timestamp timestamp, const base::PointF &target);

    void updateFallbackCursor();
    std::optional<base::PointF> touchLocation(const base::PointF &normalized) const;

    display::DisplayManager *m_displayManager = nullptr;
    Seat *m_seat = nullptr;
    EventSink *m_eventSink = nullptr;
    GestureFilter *m_gestureFilter = nullptr;
    bool m_gestureClaimed = false;
    std::set<uint32_t> m_secureKeys;
    // Physical key to the code it went down as, so that its release matches
    // whatever convention is in force by then.
    std::map<uint32_t, uint32_t> m_positionalKeys;
    std::function<void(int)> m_vtSwitch;
    // Function keys that switched the terminal; nobody saw them go down.
    std::set<uint32_t> m_vtKeys;
    bool m_motionConsumed = false;
    std::set<uint32_t> m_heldButtons;
    bool m_synthesizePending = false;
    base::Size m_bounds;
    std::vector<base::Rect> m_topology;
    base::Size m_deviceBounds;
    double m_deviceScale = 1;
    base::PointF m_pointerPosition;
    bool m_pointerPlaced = false;
    // The last layout with displays, which the pointer position refers to: a
    // reprobe republishes the layout through an empty one.
    std::vector<base::Rect> m_pointerLayout;
    std::optional<base::PointF> m_absolutePosition;
    // Where each touch point is, for the release that carries no location.
    std::map<int32_t, base::PointF> m_touchLocations;
    std::set<int32_t> m_consumedTouches;
    // The one touch point a locked session sees as its pointer.
    std::optional<int32_t> m_secureTouch;
    scene::Node *m_fallbackCursor = nullptr;
    base::Point m_fallbackCursorHotspot;
    base::WeakPtrFactory<InputManager> m_weakFactory {this};
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_INPUT_MANAGER_H_
