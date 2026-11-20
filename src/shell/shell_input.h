// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SHELL_INPUT_H_
#define CUTEFISH_SHELL_SHELL_INPUT_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "base/geometry.h"
#include "base/timer.h"
#include "ui/event.h"

namespace cutefish::compositor {
class Surface;
}

namespace cutefish::input {
class Keyboard;
class Pointer;
class Touch;
} // namespace cutefish::input

namespace cutefish::shell {

class Shell;
class ShellWindow;

// The shell's windows as one more client of the seat, so targeting, grabs,
// focus and the cursor work as for a Wayland client's surfaces.
class ShellInput
{
public:
    explicit ShellInput(Shell *shell);
    ShellInput(const ShellInput &) = delete;
    ShellInput &operator=(const ShellInput &) = delete;
    ~ShellInput();

    ShellWindow *windowForSurface(const compositor::Surface *surface) const;
    ShellWindow *pointerWindow() const { return m_pointerWindow; }
    ShellWindow *keyboardWindow() const { return m_keyboardWindow; }

    uint32_t modifiers() const;

    void cursorShapeChanged(ShellWindow *window);
    void windowDestroying(ShellWindow *window);

private:
    class PointerDelegate;
    class KeyboardDelegate;
    class TouchDelegate;
    class GestureDelegate;
    friend class PointerDelegate;
    friend class KeyboardDelegate;
    friend class TouchDelegate;
    friend class GestureDelegate;

    void applyCursor();
    void sendKey(uint32_t code, bool pressed, bool repeat);
    void stopRepeat();

    Shell *m_shell;
    std::unique_ptr<PointerDelegate> m_pointerDelegate;
    std::unique_ptr<input::Pointer> m_pointer;
    std::unique_ptr<input::Keyboard> m_keyboard;
    std::unique_ptr<TouchDelegate> m_touchDelegate;
    std::unique_ptr<input::Touch> m_touch;
    std::vector<std::unique_ptr<GestureDelegate>> m_gestureDelegates;
    ShellWindow *m_pointerWindow = nullptr;
    ShellWindow *m_keyboardWindow = nullptr;
    std::map<int32_t, ShellWindow *> m_touchPoints;
    ShellWindow *m_gestureWindow = nullptr;
    base::Timer m_repeatTimer;
    uint32_t m_repeatCode = 0;
    bool m_repeatEnabled = true;
    std::chrono::milliseconds m_repeatDelay {500};
    std::chrono::milliseconds m_repeatInterval {40};
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SHELL_INPUT_H_
