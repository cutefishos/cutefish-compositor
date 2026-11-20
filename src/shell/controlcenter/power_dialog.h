// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_POWER_DIALOG_H_
#define CUTEFISH_SHELL_CONTROLCENTER_POWER_DIALOG_H_

#include <functional>
#include <memory>
#include <string>

namespace cutefish::shell {

class Shell;
class ShellWindow;

class PowerDialog
{
public:
    enum class Action {
        Logout,
        Reboot,
        Shutdown,
    };

    explicit PowerDialog(Shell *shell);
    ~PowerDialog();

    PowerDialog(const PowerDialog &) = delete;
    PowerDialog &operator=(const PowerDialog &) = delete;

    void open(Action action);
    void close();
    bool isOpen() const;
    Action action() const { return m_action; }
    void retranslate();

    ShellWindow *window() const { return m_window.get(); }

    std::function<void()> onAccepted;
    std::function<void()> onRejected;

    static std::string title(Action action);
    static std::string confirmText(Action action);

private:
    class Content;

    void finish(bool accepted);
    void resize();

    Shell *m_shell;
    std::unique_ptr<ShellWindow> m_window;
    Content *m_content = nullptr;
    Action m_action = Action::Logout;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_POWER_DIALOG_H_
