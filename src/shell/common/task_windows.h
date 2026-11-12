// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_TASK_WINDOWS_H_
#define CUTEFISH_SHELL_COMMON_TASK_WINDOWS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"

namespace cutefish::compositor {
class MenuModel;
}

namespace cutefish::ui {
class Bitmap;
} // namespace cutefish::ui

namespace cutefish::shell {

// An application window as the dock, the menu bar and the task manager see
// it.
struct TaskWindow {
    uint64_t id = 0;
    std::string appId;
    std::string title;
    int64_t pid = 0;
    // The xdg_toplevel parent, or zero.
    uint64_t parent = 0;
    base::Rect geometry;
    // Off the task list: a window that asked for it, such as a splash.
    bool skipTaskbar = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    // Hidden by Hide or Hide Others; independent of minimized.
    bool hidden = false;
    bool responsive = true;
    // The dbusmenu the window's surface names through the appmenu protocol.
    std::string menuService;
    std::string menuPath;
    // The main menu it shows through cutefish_main_menu_v1, which takes
    // precedence; null for none.
    std::shared_ptr<compositor::MenuModel> mainMenu;
    std::shared_ptr<compositor::MenuModel> dockMenu;

    bool isTaskListWindow() const { return id != 0 && !skipTaskbar; }
};

// What changed in a window, for windowChanged.
enum TaskWindowChange : uint32_t {
    kTitleChange = 1 << 0,
    kAppIdChange = 1 << 1,
    kPidChange = 1 << 2,
    // Minimized, maximized, fullscreen or hidden.
    kStateChange = 1 << 3,
    kSkipTaskbarChange = 1 << 4,
    kGeometryChange = 1 << 5,
    kApplicationMenuChange = 1 << 6,
    kParentChange = 1 << 7,
    kResponsiveChange = 1 << 8,
};

// The application windows and what the shell may do to them. The session's
// is the window manager's (WmTaskWindows); tests use their own.
class TaskWindows
{
public:
    virtual ~TaskWindows() = default;

    // In the order they appeared.
    virtual std::vector<uint64_t> windows() const = 0;
    // Null for a window that is not, or no longer, an application window.
    virtual const TaskWindow *window(uint64_t id) const = 0;
    // The application window the user last activated, as the menu bar and
    // the dock follow it: a sheet speaks for its parent, a window off the
    // task list taking the keyboard leaves it as it was, and a shell window
    // or nothing at all taking the keyboard makes it zero.
    virtual uint64_t activeWindow() const = 0;
    // The image the window left when it was minimized; null while it is not.
    virtual const ui::Bitmap *minimizedSnapshot(uint64_t id) const = 0;

    // Unhides the window's application, restores the window and activates it:
    // what the dock's click on a task does.
    virtual void activate(uint64_t id) = 0;
    virtual void minimize(uint64_t id) = 0;
    virtual void setHidden(uint64_t id, bool hidden) = 0;
    // Asks the window to close.
    virtual void close(uint64_t id) = 0;
    // Quit, Hide and Hide Others; zero hides every application.
    virtual void quitApplication(uint64_t id) = 0;
    virtual void hideApplication(uint64_t id) = 0;
    virtual void hideOtherApplications(uint64_t keep) = 0;

    base::Signal<uint64_t> windowAdded;
    // id, TaskWindowChange bits
    base::Signal<uint64_t, uint32_t> windowChanged;
    // After the window is gone from windows().
    base::Signal<uint64_t> windowRemoved;
    base::Signal<uint64_t> activeWindowChanged;
    base::Signal<uint64_t> minimizedSnapshotChanged;
    // The minimize animation holds the window on its dock slot.
    base::Signal<uint64_t> minimizeLanded;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_TASK_WINDOWS_H_
