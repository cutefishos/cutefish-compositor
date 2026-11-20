// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTRESPONDING_NOT_RESPONDING_SHEETS_H_
#define CUTEFISH_SHELL_NOTRESPONDING_NOT_RESPONDING_SHEETS_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/signal.h"
#include "shell/shell.h"
#include "ui/view.h"
#include "wm/window_observer.h"

namespace cutefish::ui {
class DialogButton;
class Label;
class ThemeIcon;
} // namespace cutefish::ui

namespace cutefish::wm {
class Window;
}

namespace cutefish::shell {

class ShellWindow;

class NotRespondingView : public ui::View
{
public:
    static constexpr int kWidth = 400;
    static constexpr int kPadding = 16;

    NotRespondingView(std::string name, std::string icon);

    const std::string &title() const;
    void retranslate();
    double implicitHeight(double width) const;

    ui::DialogButton *forceQuitButton() const { return m_forceQuit; }
    ui::DialogButton *waitButton() const { return m_wait; }

    std::function<void()> onWait;
    std::function<void()> onForceQuit;

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    void applyTheme();

    std::string m_name;
    ui::ThemeIcon *m_icon;
    ui::Label *m_title;
    ui::Label *m_body;
    ui::DialogButton *m_forceQuit;
    ui::DialogButton *m_wait;
    uint64_t m_themeGeneration = 0;
};

// One sheet per application, gone as soon as the application answers.
class NotRespondingSheets : public Component, public wm::WindowObserver
{
public:
    explicit NotRespondingSheets(Shell *shell);
    ~NotRespondingSheets() override;

    // A window whose process is unknown stands for an application of its own.
    static int64_t applicationOf(const wm::Window *window);

    // Always offered while the shell is up.
    bool prompt(wm::Window *window);

    ShellWindow *sheet(int64_t application) const;
    NotRespondingView *view(int64_t application) const;
    uint64_t parentWindow(int64_t application) const;
    size_t sheetCount() const { return m_sheets.size(); }
    bool isWaiting(int64_t application) const { return m_waiting.count(application) != 0; }

    void wait(int64_t application);
    void forceQuit(int64_t application);

    void onWindowResponsiveChanged(wm::Window *window) override;
    void onWindowDestroying(wm::Window *window) override;

private:
    struct Sheet {
        std::unique_ptr<ShellWindow> window;
        NotRespondingView *view = nullptr;
        uint64_t parentWindow = 0;
    };

    void observe(wm::Window *window);
    void onWindowRemoved(wm::Window *window);
    void show(wm::Window *window, int64_t application);
    void hang(Sheet &sheet, wm::Window *parent);
    void close(int64_t application);
    void retranslate();

    Shell *m_shell;
    // By application: every window of a stalled client stops answering together.
    std::map<int64_t, Sheet> m_sheets;
    std::set<int64_t> m_waiting;
    std::set<wm::Window *> m_observed;
    // Closed from inside their own buttons' handlers: destroyed on the next
    // loop turn.
    std::vector<std::unique_ptr<ShellWindow>> m_closed;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTRESPONDING_NOT_RESPONDING_SHEETS_H_
