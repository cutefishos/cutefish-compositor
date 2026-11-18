// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/timer.h"
#include "shell/shell.h"
#include "ui/animation.h"

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
    void animateTo(base::Rect bounds, double opacity, double expanded, bool closing = false);
    void place(base::Rect bounds, double opacity, double expanded);
    void finishHide();
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
    std::unique_ptr<ui::Animation> m_motion;
    base::Timer m_retention;
    double m_opacity = 1;
    double m_expanded = 0;
    double m_targetExpanded = 0;
    double m_glassOpacity = -1;
    double m_glassExpanded = -1;
    base::Size m_glassSize;
    bool m_glassDark = false;
    bool m_preparing = false;
    bool m_closing = false;
    bool m_hadKeyboard = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell
