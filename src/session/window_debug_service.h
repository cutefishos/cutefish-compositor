// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

    void setInput(input::InputManager *input) { m_input = input; }
    // A real session takes that input only while the file at `path` exists, belongs to
    // root and only root may write it. Unset, the input is always taken.
    void setInputGate(std::string path) { m_inputGate = std::move(path); }
    bool acceptsInput() const;

    void setOutputStats(std::function<std::vector<std::string>()> stats)
    {
        m_outputStats = std::move(stats);
    }
    void setMemoryStats(std::function<std::vector<std::string>()> stats)
    {
        m_memoryStats = std::move(stats);
    }

    // Rects are [x, y, width, height] in logical output coordinates.
    std::string ListWindows() const;
    // False when no window has that id.
    bool MoveWindow(uint64_t id, int x, int y);
    bool SetWindowBounds(uint64_t id, int x, int y, int width, int height);
    bool ActivateWindow(uint64_t id);
    bool MinimizeWindow(uint64_t id);
    // Snaps to the left half when primary, the right half otherwise.
    bool SnapWindow(uint64_t id, bool primary);
    bool RestoreWindow(uint64_t id);
    // Empty in a headless compositor.
    std::vector<std::string> OutputStats() const;
    std::vector<std::string> MemoryStats() const;
    // In logical global coordinates and evdev codes; false while input is not accepted.
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
