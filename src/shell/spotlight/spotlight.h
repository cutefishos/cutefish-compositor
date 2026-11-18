// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/timer.h"
#include "shell/shell.h"

namespace cutefish::shell {

class ClipboardHistory;
class Dock;
class ShellWindow;
class SpotlightView;

class Spotlight : public Component
{
public:
    enum class Mode { Search, Applications, Files, Actions, Clipboard };

    Spotlight(Shell *shell, Dock *dock, std::unique_ptr<ClipboardHistory> clipboard, std::string settingsPath);
    ~Spotlight() override;

    void show(Mode mode);
    void hide();
    void toggle(Mode mode);
    void registerServices();

private:
    void fit();
    void place();
    void applyGlass();

    Shell *m_shell;
    std::unique_ptr<ClipboardHistory> m_clipboard;
    std::unique_ptr<ShellWindow> m_window;
    SpotlightView *m_view = nullptr;
    std::unique_ptr<base::dbus::Object> m_service;
    std::unique_ptr<base::dbus::Object> m_legacy;
    std::vector<base::Subscription> m_subscriptions;
    std::string m_output;
    base::Rect m_target;
    base::Timer m_motion;
    base::Timer m_retention;
    std::chrono::steady_clock::time_point m_motionStart;
    int m_initialHeight = 0;
    int m_initialYOffset = 0;
    base::Size m_glassSize;
    bool m_glassIdle = false;
    bool m_glassDark = false;
    bool m_hadKeyboard = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell
