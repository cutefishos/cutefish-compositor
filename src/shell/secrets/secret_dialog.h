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

class SecretDialog
{
public:
    explicit SecretDialog(Shell *shell);
    virtual ~SecretDialog();

    SecretDialog(const SecretDialog &) = delete;
    SecretDialog &operator=(const SecretDialog &) = delete;

    void open();
    // Hides it at once and lets its window go on the next loop turn.
    void close();
    bool isOpen() const;
    void refresh();
    // What was typed and the request stay.
    void retranslate();
    ShellWindow *window() const { return m_window.get(); }
    ui::DialogButton *cancelButton() const;
    ui::DialogButton *primaryButton() const;

    std::function<void()> onDismissed;

    static constexpr double kWidth = 360;
    static constexpr double kMargin = 16;
    static constexpr double kSpacing = 16;
    static constexpr double kTextSize = 14;
    static constexpr double kBodyLineHeight = 20;

protected:
    class Frame;

    struct Row {
        ui::View *view = nullptr;
        // Replaces kSpacing above the row.
        double spacing = kSpacing;
        bool fillWidth = true;
    };

    virtual void build(ui::View *frame) = 0;
    virtual ui::View *initialFocus() const = 0;
    virtual void accept() = 0;
    // Called by refresh().
    virtual void update() = 0;
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
