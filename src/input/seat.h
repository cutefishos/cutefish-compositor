// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/seat.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_SEAT_H_
#define CUTEFISH_INPUT_SEAT_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>


#include "clipboard/selection.h"
#include "base/geometry.h"
#include "compositor/surface_observer.h"
#include "input/cursor_shape.h"
#include "input/drag.h"
#include "input/pointer_gesture_delegate.h"
#include "input/pointer_constraints.h"
#include "input/shortcuts_inhibition.h"
#include "input/types.h"
#include "input/key_translator.h"
#include "input/xkb_tracker.h"

namespace cutefish::compositor {
class Compositor;
}

namespace cutefish::scene {
class Node;
}

namespace cutefish::input {

class Keyboard;
class Pointer;
struct RelativeMotion;
class SeatObserver;
class SessionGate;
class Touch;

class Seat : public compositor::SurfaceObserver
{
public:
    explicit Seat(compositor::Compositor *compositor);
    Seat(const Seat &) = delete;
    Seat &operator=(const Seat &) = delete;
    ~Seat() override;

    // Called before the clients are torn down, so a Pointer or Keyboard being destroyed cannot re-enter.
    void shutdown();

    bool wasShutdown() const { return m_wasShutdown; }

    compositor::Compositor *compositor() const { return m_compositor; }

    // The keys as the compositor sees them, which is what its own shortcuts
    // match: under the Command convention, Command is Meta here.
    const XkbTracker *xkbTracker() const { return m_xkbTracker.get(); }

    XkbTracker *xkbTracker() { return m_xkbTracker.get(); }

    // The keys as clients see them, after the KeyTranslator: wl_keyboard and
    // the input method take their modifiers from here.
    const XkbTracker *clientXkbTracker() const { return m_clientXkbTracker.get(); }

    KeyTranslator *keyTranslator() { return &m_keyTranslator; }

    ShortcutConvention shortcutConvention() const { return m_keyTranslator.convention(); }

    void setShortcutConvention(ShortcutConvention convention)
    {
        m_keyTranslator.setConvention(convention);
    }

    // Keys held across the change stay held in the new state.
    bool setKeyboardLayout(const std::string &name);

    const KeyRepeatSettings &keyRepeatSettings() const { return m_keyRepeatSettings; }

    void setKeyRepeatSettings(const KeyRepeatSettings &settings);

    void addObserver(SeatObserver *observer);
    void removeObserver(SeatObserver *observer);

    scene::Node *dragIconContainer() const { return m_dragIconContainer; }

    void setDragIconContainer(scene::Node *container) { m_dragIconContainer = container; }

    // Null leaves the cursor unrendered.
    scene::Node *cursorContainer() const { return m_cursorContainer; }

    void setCursorContainer(scene::Node *container) { m_cursorContainer = container; }

    // Image presence and cursor ownership differ: a valid null request owns
    // a hidden cursor. Only absence of an owned request permits the fallback.
    bool hasClientCursor() const;
    bool hasClientCursorRequest() const;
    void notifyCursorChanged();
    std::optional<CursorShape> clientCursorShape() const;

    CursorShape compositorCursorShape() const { return m_compositorCursorShape; }

    void setCompositorCursorShape(CursorShape shape);

    std::optional<CursorShape> cursorOverride() const { return m_cursorOverride; }

    void setCursorOverride(std::optional<CursorShape> shape);

    base::PointF cursorPosition() const { return m_cursorPosition; }

    // A touch hides the pointer until the mouse moves, clicks or scrolls again.
    bool cursorHiddenByTouch() const { return m_cursorHiddenByTouch; }

    // Hiding also takes the pointer's focus away, so that no hover is left.
    void setCursorHiddenByTouch(bool hidden);

    void traceCursors(const char *transition) const;
    void updateCursorPosition(const base::PointF &position);
    void releasePointerButton(Timestamp timestamp, uint32_t evdevButton);
    void updatePointerButtonState(uint32_t evdevButton, bool pressed);
    void endPointerGrabIfReleased();

    void consumeDragSerials() { m_dragSerials.clear(); }

    void recordDragSerial(uint32_t serial, ButtonFlags button);
    ButtonFlags dragSerial(compositor::Surface *origin, uint32_t serial) const;

    DragController *drag() { return &m_drag; }

    PointerConstraints *pointerConstraints() { return &m_pointerConstraints; }

    ShortcutsInhibition *shortcutsInhibition() { return &m_shortcutsInhibition; }

    // Breaks the focused window's pointer constraint and shortcuts inhibitor until the next click on it.
    // Returns whether either was active.
    bool breakInputCapture();

    clipboard::SelectionController *primarySelection() { return &m_primarySelection; }

    clipboard::SelectionController *selection() { return &m_selection; }

    // Null leaves the seat unrestricted.
    void setSessionGate(SessionGate *gate);

    SessionGate *sessionGate() const { return m_sessionGate; }

    // Every focus path goes through this, so a locked session has one place to fail closed.
    bool allowsFocus(const compositor::Surface *surface) const;

    compositor::Surface *focusedSurface() const { return m_focusedSurface; }

    bool focusIsBorrowed() const { return m_focusBorrowed; }

    compositor::Surface *pointerFocus() const;

    // `surface` is expected to be a root surface. A `borrowed` focus takes the keys without a leave
    // for the client that has it, since toolkits deactivate on one.
    void setFocusedSurface(compositor::Surface *surface, bool borrowed = false);

    void clearPointerFocus();

    // The surface a press was delivered to, which keeps every pointer event until the last button is released.
    compositor::Surface *pointerGrabSurface() const { return m_pointerGrabSurface; }

    // Does not tell clients: dispatchKey() sends each key, then the modifiers it changed, since
    // clients read a modifier key's event against the state before it.
    bool updateKeyState(uint32_t evdevCode, bool pressed);
    void notifyKeyboardModifiers();
    // After a key the clients never got: a Caps Lock toggled on the lock
    // screen must reach them and the lights too, or the two states drift.
    void syncKeyboardLocks();
    void dispatchKey(Timestamp timestamp, uint32_t evdevCode, bool pressed);

    using KeyFilter = std::function<bool(Timestamp, uint32_t, bool)>;

    void setInputMethodFilter(KeyFilter filter) { m_inputMethodFilter = std::move(filter); }

    // For a key that already went through the KeyTranslator.
    void dispatchKeyToClients(Timestamp timestamp, uint32_t evdevCode, bool pressed);

    // Presses `codes` in order and releases them in reverse, as keys clients
    // see: past the compositor's shortcuts and the KeyTranslator.
    void tapKeys(Timestamp timestamp, const std::vector<uint32_t> &codes);

    bool keySuppressed(uint32_t code) const { return m_suppressedKeys.contains(code); }

    void suppressKey(uint32_t code) { m_suppressedKeys.insert(code); }

    // The delivered keys clients hold pressed, less those the input method took.
    std::map<uint32_t, KeyState> clientPressedKeys() const;

    // Every pointer event carries the location, so a button before any motion is still placed correctly.
    // `relative` is the physical motion behind this location, if there was one.
    void onPointerMotion(Timestamp timestamp, const base::PointF &rootLocation,
                         const RelativeMotion *relative = nullptr);
    void onPointerButton(Timestamp timestamp, const base::PointF &rootLocation, uint32_t evdevButton,
                         bool pressed);
    void onPointerAxis(Timestamp timestamp, const base::PointF &rootLocation, const base::PointF &offset,
                       bool discrete);
    void onPointerAxisStop(Timestamp timestamp, const base::PointF &rootLocation);
    void onPointerGestureBegin(Timestamp timestamp, const base::PointF &rootLocation,
                               GestureType type, uint32_t fingers);
    void onPointerGestureUpdate(Timestamp timestamp, GestureType type,
                                const GestureUpdate &update);
    void onPointerGestureEnd(Timestamp timestamp, GestureType type, bool cancelled);
    void onKey(Timestamp timestamp, uint32_t evdevCode, bool pressed);

    // A new touch point is targeted once, at the surface it lands on. `rootLocation` is in output coordinates.
    void onTouchDown(Timestamp timestamp, int32_t id, const base::PointF &rootLocation);
    void onTouchMotion(Timestamp timestamp, int32_t id, const base::PointF &rootLocation);
    void onTouchUp(Timestamp timestamp, int32_t id);
    void onTouchCancel();
    void onTouchFrame();

    struct TouchPoint {
        int32_t id = 0;
        base::PointF location;
    };

    // What a move or resize requested with a touch-down serial follows.
    std::optional<TouchPoint> touchPointOn(const compositor::Surface *root) const;
    // Takes point `id` from the clients that have it. Each is sent a cancel,
    // which ends every point it had.
    void takeTouchPoint(int32_t id);

    base::PointF pointerLocation() const { return m_pointerLocation; }

    // Cached for one pointer motion; a press, whose activation may restack, searches afresh.
    compositor::Surface *pointerTargetAt(scene::Node *root, const base::PointF &location,
                                         base::PointF *surfaceLocation);
    void beginPointerMotion();
    void endPointerMotion();

    ButtonFlags pressedButtons() const { return m_pressedButtons; }

    // Keyed by physical evdev code.
    const std::map<uint32_t, KeyState> &pressedKeys() const { return m_pressedKeys; }

    void addPointer(Pointer *pointer);
    void removePointer(Pointer *pointer);
    void addKeyboard(Keyboard *keyboard);
    void removeKeyboard(Keyboard *keyboard);
    void addTouch(Touch *touch);
    void removeTouch(Touch *touch);

    void onSurfaceDestroying(compositor::Surface *surface) override;

private:
    void setPointerGrabSurface(compositor::Surface *surface);

    // Focus and the implicit grab observe the same surfaces, so an observer is
    // only dropped once neither refers to it.
    void maybeRemoveSurfaceObserver(compositor::Surface *surface);

    void notifySurfaceFocused(compositor::Surface *gainedFocus, compositor::Surface *lostFocus);

    // One key out of the KeyTranslator: into the client xkb state, then to
    // the input method or the clients, then the modifiers it changed.
    void deliverKey(Timestamp timestamp, uint32_t evdevCode, bool pressed);

    std::map<uint32_t, ButtonFlags> m_dragSerials;
    DragController m_drag;
    PointerConstraints m_pointerConstraints;
    ShortcutsInhibition m_shortcutsInhibition;
    clipboard::SelectionController m_selection;
    clipboard::SelectionController m_primarySelection;
    compositor::Compositor *m_compositor = nullptr;
    scene::Node *m_cursorContainer = nullptr;
    scene::Node *m_dragIconContainer = nullptr;
    std::unique_ptr<XkbTracker> m_xkbTracker;
    std::unique_ptr<XkbTracker> m_clientXkbTracker;
    KeyTranslator m_keyTranslator;
    KeyRepeatSettings m_keyRepeatSettings;

    std::vector<SeatObserver *> m_observers;
    std::vector<Pointer *> m_pointers;
    std::vector<Keyboard *> m_keyboards;
    std::vector<Touch *> m_touches;
    bool m_cursorHiddenByTouch = false;

    struct DeliveredTouch {
        base::PointF location;
        std::shared_ptr<compositor::Surface *> surface;
        uint64_t order = 0;
    };

    std::map<int32_t, DeliveredTouch> m_touchPoints;
    uint64_t m_touchOrder = 0;

    SessionGate *m_sessionGate = nullptr;
    compositor::Surface *m_focusedSurface = nullptr;
    bool m_focusBorrowed = false;
    compositor::Surface *m_pointerGrabSurface = nullptr;

    base::PointF m_pointerLocation;

    struct PointerTarget {
        scene::Node *root;
        base::PointF location;
        std::shared_ptr<compositor::Surface *> surface;
        base::PointF surfaceLocation;
    };

    bool m_inPointerMotion = false;
    std::optional<PointerTarget> m_pointerTarget;
    base::PointF m_cursorPosition;
    CursorShape m_compositorCursorShape = CursorShape::Default;
    std::optional<CursorShape> m_cursorOverride;
    ButtonFlags m_pressedButtons = NoButton;
    std::map<uint32_t, KeyState> m_pressedKeys;

    KeyFilter m_inputMethodFilter;
    std::set<uint32_t> m_inputMethodKeys;
    bool m_wasShutdown = false;
    std::set<uint32_t> m_suppressedKeys;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_SEAT_H_
