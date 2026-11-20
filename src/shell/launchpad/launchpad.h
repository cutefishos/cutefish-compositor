// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_H_
#define CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "shell/launchpad/launchpad_model.h"

namespace cutefish::shell {

class Dock;
class TaskManager;

// Exports org.cutefish.Launchpad at /Launchpad.
class Launchpad
{
public:
    // `dock` may be null.
    Launchpad(base::dbus::Connection *bus, TaskManager &tasks, ApplicationLauncher &launcher, Dock *dock,
              std::string settingsPath);
    ~Launchpad();

    Launchpad(const Launchpad &) = delete;
    Launchpad &operator=(const Launchpad &) = delete;

    bool registerService();

    LaunchpadModel &applications() { return m_applications; }

    std::function<void()> showWindow;
    std::function<void()> hideWindow;

    bool isShown() const { return m_shown; }
    void show();
    void hide();
    // Follows the logical state: the window can still be mapped while the
    // compositor unmaps it.
    void toggle();

    bool isPinnedInDock(const std::string &desktop) const;
    void sendToDock(const std::string &desktop);
    void removeFromDock(const std::string &desktop);

    std::vector<std::string> takeOpenedApplications();

    // A window-level drag can cost the launchpad its activation; while one
    // runs, losing it does not hide the launchpad.
    bool isDragging() const { return m_dragging; }
    void setDragging(bool dragging);
    void deactivated();

    base::Signal<const std::string &> applicationOpened;

private:
    void noteApplicationOpened(const std::string &application);

    base::dbus::Connection *m_bus;
    TaskManager &m_tasks;
    Dock *m_dock;
    LaunchpadModel m_applications;
    std::unique_ptr<base::dbus::Object> m_object;
    bool m_shown = false;
    bool m_dragging = false;
    bool m_openedTaken = false;
    std::vector<std::string> m_openedApplications;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_H_
