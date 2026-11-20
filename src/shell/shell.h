// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SHELL_H_
#define CUTEFISH_SHELL_SHELL_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "base/timer.h"

namespace cutefish::base::dbus {
class Connection;
}

namespace cutefish::compositor {
class Compositor;
}

namespace cutefish::decoration {
class Theme;
}

namespace cutefish::display {
class DisplayManager;
}

namespace cutefish::input {
class Seat;
}

namespace cutefish::wayland {
class InputMethodRelay;
}

namespace cutefish::wm {
class WindowManager;
}

namespace cutefish::shell {

class ApplicationBadges;
class ApplicationLauncher;
class Applications;
class ShellHost;
class ShellInput;
class TaskManager;
class TaskWindows;
class ShellWindow;
class WindowRenderer;

// Destroyed before anything it uses.
class Component
{
public:
    virtual ~Component() = default;
};

struct ShellEnvironment {
    compositor::Compositor *compositor = nullptr;
    wm::WindowManager *windowManager = nullptr;
    input::Seat *seat = nullptr;
    // May be null.
    base::dbus::Connection *bus = nullptr;
    // Null for none.
    wayland::InputMethodRelay *inputMethod = nullptr;
    // Null keeps the toolkit's light theme.
    decoration::Theme *theme = nullptr;
    // Empty reads the user's.
    std::string servicesSettings;
    // May be null.
    std::function<std::string()> createActivationToken;
    // Null where there is no keyboard.
    std::function<std::string(const std::string &key, uint32_t modifiers, bool activeWindow)> shortcutText;
};

// Created by CompositorShell outside the greeter.
class Shell
{
public:
    struct Output {
        // Empty for the single output of a compositor without a display
        // manager.
        std::string name;
        base::Rect bounds;
        double scale = 1;
        bool primary = false;

        bool operator==(const Output &) const = default;
    };

    explicit Shell(const ShellEnvironment &environment);
    Shell(const Shell &) = delete;
    Shell &operator=(const Shell &) = delete;
    ~Shell();

    compositor::Compositor *compositor() const { return m_environment.compositor; }
    wm::WindowManager *windowManager() const { return m_environment.windowManager; }
    input::Seat *seat() const { return m_environment.seat; }
    base::dbus::Connection *bus() const { return m_environment.bus; }
    std::string shortcutText(const std::string &key, uint32_t modifiers, bool activeWindow) const
    {
        return m_environment.shortcutText ? m_environment.shortcutText(key, modifiers, activeWindow) : std::string();
    }
    display::DisplayManager *displayManager() const;

    ShellInput *input() const { return m_input.get(); }
    ShellHost *host() const { return m_host.get(); }
    WindowRenderer *renderer() const { return m_renderer.get(); }

    // The primary one first.
    const std::vector<Output> &outputs() const { return m_outputs; }
    // The primary one for an empty or unknown name; null when there is no
    // output.
    const Output *output(const std::string &name) const;
    const Output *outputAt(const base::Point &point) const;
    base::Signal<> outputsChanged;
    base::Rect workArea(const Output &output) const;

    Applications &applications();
    TaskWindows &taskWindows();
    TaskManager &tasks();
    ApplicationLauncher &launcher();
    ApplicationBadges &badges();
    void openSettings(const std::vector<std::string> &arguments = {});
    // Tests hand in their own before the first use.
    void setApplications(std::unique_ptr<Applications> applications);
    void setTaskWindows(std::unique_ptr<TaskWindows> windows);

    // Components are destroyed in reverse order of addition, before the shell.
    template <typename T>
    T *addComponent(std::unique_ptr<T> component)
    {
        T *result = component.get();
        m_components.push_back(std::move(component));
        return result;
    }

    void releaseFreedMemorySoon();

    void addWindow(ShellWindow *window);
    void removeWindow(ShellWindow *window);
    const std::vector<ShellWindow *> &windows() const { return m_windows; }

private:
    class ThemeFollower;

    void updateOutputs();

    const ShellEnvironment m_environment;
    std::vector<Output> m_outputs;
    std::vector<ShellWindow *> m_windows;
    std::unique_ptr<WindowRenderer> m_renderer;
    std::unique_ptr<ShellInput> m_input;
    std::unique_ptr<ShellHost> m_host;
    std::unique_ptr<ThemeFollower> m_theme;
    std::unique_ptr<Applications> m_applications;
    std::unique_ptr<TaskWindows> m_taskWindows;
    std::unique_ptr<TaskManager> m_tasks;
    std::unique_ptr<ApplicationLauncher> m_launcher;
    std::unique_ptr<ApplicationBadges> m_badges;
    base::Subscription m_outputsChanged;
    base::Subscription m_languageChanged;
    base::Timer m_trimTimer;
    // Last: the first to go.
    std::vector<std::unique_ptr<Component>> m_components;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SHELL_H_
