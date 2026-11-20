// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SHELL_HOST_H_
#define CUTEFISH_SHELL_SHELL_HOST_H_

#include <map>
#include <memory>
#include <vector>

#include "ui/embedded_host.h"

namespace cutefish::wayland {
class InputMethodRelay;
}

namespace cutefish::shell {

class Shell;
class ShellDrag;
class ShellTextInput;
class ShellWindow;

class ShellHost final : public ui::EmbeddedHost
{
public:
    // `inputMethod` may be null.
    ShellHost(Shell *shell, wayland::InputMethodRelay *inputMethod);
    ShellHost(const ShellHost &) = delete;
    ShellHost &operator=(const ShellHost &) = delete;
    ~ShellHost() override;

    ShellWindow *windowFor(const ui::EmbeddedWindow *window) const;
    ShellDrag *drag() const { return m_drag.get(); }

    void windowDestroying(ShellWindow *window);
    void frameStarting(ShellWindow *window);

    void popupShown(ui::EmbeddedWindow *popup) override;
    void popupRepositioned(ui::EmbeddedWindow *popup) override;
    void popupHidden(ui::EmbeddedWindow *popup) override;
    void activationRequested(ui::EmbeddedWindow *window) override;
    void textInputChanged(ui::EmbeddedWindow *window, const ui::TextInputState *state,
                          bool fromInputMethod) override;
    void blurChanged(ui::EmbeddedWindow *window) override;
    bool startDrag(ui::EmbeddedWindow *window, ui::DragSource source) override;

private:
    Shell *m_shell;
    std::map<const ui::EmbeddedWindow *, std::unique_ptr<ShellWindow>> m_popups;
    // Popups let go of, destroyed on the next loop turn: the toolkit hides a
    // popup from inside input the seat is still dispatching to its surface.
    std::vector<std::unique_ptr<ShellWindow>> m_released;
    std::map<const ui::EmbeddedWindow *, bool> m_glassDark;
    std::unique_ptr<ShellDrag> m_drag;
    std::unique_ptr<ShellTextInput> m_textInput;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SHELL_HOST_H_
