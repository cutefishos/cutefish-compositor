// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SECRETS_SECRET_DIALOG_H_
#define CUTEFISH_SHELL_SECRETS_SECRET_DIALOG_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/view.h"

namespace cutefish::ui {
class DialogButton;
class Label;
} // namespace cutefish::ui

namespace cutefish::shell {

class Shell;
class ShellWindow;

// The secret prompts (Wi-Fi and VPN passwords, Bluetooth pairing): a window
// in FishUI's look, kept above applications and
// centred by the window manager, 360 wide and as tall as its content; a
// bold title and a grey explanation, the dialog's own rows, Cancel and a
// primary button at the bottom right. Escape, Cancel and the window
// manager's close all dismiss it; a drag anywhere but on a control moves the window.
class SecretDialog
{
public:
    explicit SecretDialog(Shell *shell);
    virtual ~SecretDialog();

    SecretDialog(const SecretDialog &) = delete;
    SecretDialog &operator=(const SecretDialog &) = delete;

    // Shows the dialog, or raises it and gives it the keyboard when shown.
    void open();
    // Hides it at once and lets its window go on the next loop turn.
    void close();
    bool isOpen() const;
    // update(), then lays out again and fits the window to the content.
    void refresh();
    // Takes its text again after the session language changed; what was
    // typed and the request stay.
    void retranslate();
    ShellWindow *window() const { return m_window.get(); }
    ui::DialogButton *cancelButton() const;
    ui::DialogButton *primaryButton() const;

    // Escape, Cancel or the window manager closed it; it is closed.
    std::function<void()> onDismissed;

    // The frame's fixed width and margins (FishUI's dialog metrics).
    static constexpr double kWidth = 360;
    static constexpr double kMargin = 16;
    static constexpr double kSpacing = 16;
    static constexpr double kTextSize = 14;
    static constexpr double kBodyLineHeight = 20;

protected:
    class Frame;

    // A row between the text and the buttons, in the order added.
    struct Row {
        ui::View *view = nullptr;
        // The gap above it instead of kSpacing (a Layout.topMargin).
        double spacing = kSpacing;
        bool fillWidth = true;
    };

    // Builds the dialog's own rows into `frame` (their parent).
    virtual void build(ui::View *frame) = 0;
    // The view to give the keyboard to on opening.
    virtual ui::View *initialFocus() const = 0;
    virtual void accept() = 0;
    // The title, the text, the rows' state and the buttons from the request
    // shown now; called by refresh().
    virtual void update() = 0;
    // The rows' own text that update() does not set.
    virtual void retranslateRows() {}

    void setTitle(const std::string &title);
    void setText(const std::string &text);
    void setRows(std::vector<Row> rows);
    void dismiss();

private:
    Shell *m_shell;
    std::unique_ptr<ShellWindow> m_window;
    Frame *m_frame = nullptr;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SECRETS_SECRET_DIALOG_H_
