// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SEARCH_SEARCH_WINDOW_H_
#define CUTEFISH_SHELL_SEARCH_SEARCH_WINDOW_H_

#include <memory>
#include <vector>

#include "base/dbus.h"
#include "base/geometry.h"
#include "base/signal.h"
#include "base/timer.h"
#include "shell/shell.h"
#include "shell/search/search_model.h"

namespace cutefish::shell {

class SearchView;
class ShellWindow;

// Spotlight's panel: applications and files found as the words are typed, from
// anywhere, with Alt+Space. As a Picker window it only borrows the keyboard,
// and losing it closes the panel.
class SearchWindow : public Component
{
public:
    SearchWindow(Shell *shell, base::dbus::Connection *bus);
    ~SearchWindow() override;

    ShellWindow *window() const { return m_window.get(); }
    SearchView *view() const { return m_view; }
    SearchModel &model() const { return *m_model; }

    bool isOpen() const;
    void open();
    void close();
    void toggle();

    // org.cutefish.Search: the shortcut's way in, from where the action runs.
    bool registerService();

private:
    base::Rect geometry() const;
    void place();
    void applyGlass();
    // Grows or shrinks the panel, and fades the content in or out, on the
    // loop; the glass follows every step.
    void animate(int targetHeight, double targetOpacity, bool hideWhenDone);
    void tickAnimation();
    void activate(int index);

    Shell *m_shell;
    base::dbus::Connection *m_bus = nullptr;
    std::unique_ptr<SearchModel> m_model;
    std::unique_ptr<ShellWindow> m_window;
    SearchView *m_view = nullptr;
    std::unique_ptr<base::dbus::Object> m_object;
    std::vector<base::Subscription> m_subscriptions;
    int m_glassDark = -1;
    int m_glassHeight = -1;
    base::Timer m_animation;
    int m_height = 0;
    double m_animationProgress = 0;
    double m_heightFrom = 0;
    double m_heightTo = 0;
    double m_opacityFrom = 1;
    double m_opacityTo = 1;
    bool m_animating = false;
    bool m_hideWhenDone = false;
    bool m_hadKeyboard = false;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SEARCH_SEARCH_WINDOW_H_
