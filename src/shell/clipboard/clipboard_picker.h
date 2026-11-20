// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_PICKER_H_
#define CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_PICKER_H_

#include <memory>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "shell/shell.h"

namespace cutefish::shell {

class ClipboardHistory;
class ClipboardPickerView;
class ShellWindow;

// As a Picker window it only borrows the keyboard: the application in front stays
// active and receives the paste. Losing the keyboard while shown closes it.
class ClipboardPicker : public Component
{
public:
    ClipboardPicker(Shell *shell, std::unique_ptr<ClipboardHistory> history);
    ~ClipboardPicker() override;

    ClipboardHistory &history() const { return *m_history; }
    ShellWindow *window() const { return m_window.get(); }
    ClipboardPickerView *view() const { return m_view; }

    bool isOpen() const;
    void open();
    void close();
    void toggle();

    base::Rect geometry() const;

private:
    void place();
    void applyGlass();

    Shell *m_shell;
    std::unique_ptr<ClipboardHistory> m_history;
    std::unique_ptr<ShellWindow> m_window;
    ClipboardPickerView *m_view = nullptr;
    // The keyboard arrives after the window maps; only losing it counts.
    bool m_hadKeyboard = false;
    int m_glassDark = -1;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_PICKER_H_
