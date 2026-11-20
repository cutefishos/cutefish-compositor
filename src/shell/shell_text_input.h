// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SHELL_TEXT_INPUT_H_
#define CUTEFISH_SHELL_SHELL_TEXT_INPUT_H_

#include "ui/event.h"
#include "wayland/protocols/text_input.h"

namespace cutefish::shell {

class ShellWindow;

// The shell's text fields on the input-method relay, next to text-input-v3 and
// v1.
class ShellTextInput final : public wayland::TextInput
{
public:
    explicit ShellTextInput(wayland::InputMethodRelay *relay);
    ~ShellTextInput() override;

    // A null `state` disables.
    void update(ShellWindow *window, const ui::TextInputState *state, bool fromInputMethod);

    void windowDestroying(ShellWindow *window);

    bool attached() const override { return m_window != nullptr; }
    bool ownsSurface(const compositor::Surface *surface) const override;
    void sendEdit(const wayland::TextInputEdit &edit) override;
    void detached() override;
    void focusChanged(compositor::Surface *gained, compositor::Surface *lost) override;

private:
    ShellWindow *m_window = nullptr;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SHELL_TEXT_INPUT_H_
