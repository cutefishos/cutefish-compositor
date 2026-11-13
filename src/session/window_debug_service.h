// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// org.cutefish.WindowDebug1: the window list and direct window placement, for
// checking window-level behaviour from a script instead of by hand. Modelled on
// ash/debug.cc and chromeos/ui/wm/debug_util.cc (PrintWindowHierarchy), which
// dump the same facts for ChromeOS.

#pragma once

#include "base/dbus.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cutefish::input {
class InputManager;
}

namespace cutefish::wm {
class WindowManager;
}

namespace cutefish::session {

class WindowDebugService
{
public:
    // The session bus when no connection is given.
    explicit WindowDebugService(wm::WindowManager *manager, base::dbus::Connection *bus = nullptr);
    WindowDebugService(const WindowDebugService &) = delete;
    WindowDebugService &operator=(const WindowDebugService &) = delete;
    ~WindowDebugService();

    bool registerService();

    // The input methods below feed it as a device would.
    void setInput(input::InputManager *input) { m_input = input; }
    // A real session takes that input only while the file at `path` exists,
    // belongs to root and only root may write it: whoever can make it can
    // already inject input through uinput, so a caller gains nothing new.
    // Unset, as headless, the input is always taken.
    void setInputGate(std::string path) { m_inputGate = std::move(path); }
    bool acceptsInput() const;

    // Supplies one scheduling stats line per output, as the compositor logs on
    // seat changes and at exit.
    void setOutputStats(std::function<std::vector<std::string>()> stats)
    {
        m_outputStats = std::move(stats);
    }
    // Supplies what the compositor holds in memory, a line per measure.
    void setMemoryStats(std::function<std::vector<std::string>()> stats)
    {
        m_memoryStats = std::move(stats);
    }

    // Every window bottom to top, as a JSON array. Rects are [x, y, width,
    // height] in logical output coordinates.
    std::string ListWindows() const;
    // Each goes through the window's state machine, as a user's move would.
    // False when no window has that id.
    bool MoveWindow(uint64_t id, int x, int y);
    bool SetWindowBounds(uint64_t id, int x, int y, int width, int height);
    bool ActivateWindow(uint64_t id);
    bool MinimizeWindow(uint64_t id);
    // Snaps to the left half when primary, the right half otherwise.
    bool SnapWindow(uint64_t id, bool primary);
    bool RestoreWindow(uint64_t id);
    // The outputs' scheduling counters, one line each, read live. Empty in a
    // headless compositor.
    std::vector<std::string> OutputStats() const;
    // The heap, the GL textures by size and owner, Skia's cache and the
    // wallpaper decodes, read live.
    std::vector<std::string> MemoryStats() const;
    // Pointer and keyboard input for driving the compositor from a script,
    // in logical global coordinates and evdev codes. False while
    // acceptsInput() is.
    bool PointerMotion(double x, double y);
    bool PointerButton(uint32_t button, bool pressed);
    bool Key(uint32_t code, bool pressed);

private:
    wm::WindowManager *m_manager;
    input::InputManager *m_input = nullptr;
    std::string m_inputGate;
    std::function<std::vector<std::string>()> m_outputStats;
    std::function<std::vector<std::string>()> m_memoryStats;
    base::dbus::Connection *m_bus;
    std::unique_ptr<base::dbus::Object> m_object;
    bool m_registered = false;
};

} // namespace cutefish::session
