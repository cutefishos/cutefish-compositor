// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_WINDOW_H_
#define CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_WINDOW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"
#include "shell/shell.h"

namespace cutefish::shell {

class Dock;
class Launchpad;
class LaunchpadView;
class LaunchpadWallpaper;
class Wallpaper;
class ShellWindow;

// The launchpad window: a window of
// the Launchpad role covering the primary output, menu bar included, with
// room left for the dock; shown, activated and hidden by the Launchpad it is
// given, and hidden again when it loses the activation to anything but its
// own menu, a drag or the capture tools. The compositor animates its
// opening and closing by its role.
class LaunchpadWindow : public Component
{
public:
    // `dock` gives the room to leave; it may be null.
    // `wallpaper` is the desktop's, whose decode the launchpad shrinks.
    LaunchpadWindow(Shell *shell, std::unique_ptr<Launchpad> launchpad, Dock *dock, Wallpaper *wallpaper);
    ~LaunchpadWindow() override;

    Launchpad &launchpad() { return *m_launchpad; }
    ShellWindow *window() const { return m_window.get(); }
    LaunchpadView *view() const { return m_view; }
    LaunchpadWallpaper &wallpaper() { return *m_wallpaper; }

private:
    void showWindow();
    void hideWindow();
    void updateGeometry();
    void activeChanged();
    bool keepsShownWhileInactive() const;

    Shell *m_shell;
    std::unique_ptr<Launchpad> m_launchpad;
    Dock *m_dock;
    std::unique_ptr<LaunchpadWallpaper> m_wallpaper;
    std::unique_ptr<ShellWindow> m_window;
    LaunchpadView *m_view = nullptr;
    std::vector<base::Subscription> m_subscriptions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_WINDOW_H_
