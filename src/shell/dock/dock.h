// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_DOCK_DOCK_H_
#define CUTEFISH_SHELL_DOCK_DOCK_H_

#include <functional>
#include <memory>
#include <string>

#include "base/dbus.h"
#include "base/geometry.h"
#include "base/signal.h"
#include "shell/common/running_applications.h"
#include "shell/dock/dock_model.h"
#include "shell/dock/dock_settings.h"
#include "shell/dock/minimized_windows.h"
#include "shell/dock/trash_monitor.h"

namespace cutefish::shell {

// Exports org.cutefish.Dock at /Dock, through which Settings changes the dock's
// settings.
class Dock
{
public:
    // Everything must outlive the dock.
    Dock(base::dbus::Connection *bus, TaskManager &tasks, ApplicationLauncher &launcher, std::string settingsPath,
         std::unique_ptr<TrashMonitor> trash = {});
    ~Dock();

    Dock(const Dock &) = delete;
    Dock &operator=(const Dock &) = delete;

    bool registerService();

    DockModel &applications() { return m_applications; }
    RunningApplications &running() { return m_running; }
    DockSettings &settings() { return m_settings; }
    MinimizedWindows &minimizedWindows() { return m_minimizedWindows; }
    TrashMonitor &trash() { return *m_trash; }

    // Empty without a view.
    std::function<base::Rect()> primaryGeometry;
    void notifyPrimaryGeometryChanged();
    base::Signal<> primaryGeometryChanged;

private:
    base::dbus::Connection *m_bus;
    DockSettings m_settings;
    RunningApplications m_running;
    DockModel m_applications;
    MinimizedWindows m_minimizedWindows;
    std::unique_ptr<TrashMonitor> m_trash;
    std::unique_ptr<base::dbus::Object> m_object;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_DOCK_DOCK_H_
