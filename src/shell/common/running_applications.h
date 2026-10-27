// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_RUNNING_APPLICATIONS_H_
#define CUTEFISH_SHELL_COMMON_RUNNING_APPLICATIONS_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "shell/common/applications.h"

namespace cutefish::shell {

class TaskManager;

// Applications that keep running without a window, as those built for that
// model do: each owns org.cutefish.Application.<id> on the session bus for
// as long as it runs. Its object at /org/cutefish/Application, interface
// org.cutefish.Application, holds the AppId its windows carry and whether it
// ShowsInDock, and quits on Quit(). Applications are named as the task
// manager names them: by desktop id, else by app id.
class RunningApplications
{
public:
    static constexpr const char *kNamePrefix = "org.cutefish.Application.";
    static constexpr const char *kPath = "/org/cutefish/Application";
    static constexpr const char *kInterface = "org.cutefish.Application";

    // `bus` may be null: nothing runs then. The task manager must outlive
    // this.
    RunningApplications(base::dbus::Connection *bus, TaskManager &tasks);
    ~RunningApplications();

    RunningApplications(const RunningApplications &) = delete;
    RunningApplications &operator=(const RunningApplications &) = delete;

    // The applications that run and show in the dock, in the order they
    // were found.
    std::vector<std::string> applications() const;
    bool showsInDock(const std::string &application) const;
    // Null for an application no desktop entry claims.
    DesktopEntryPtr entry(const std::string &application) const;
    // Asks every instance of the application to quit.
    void quit(const std::string &application) const;
    // Asks the instances whose AppId is `appId`, ignoring ASCII case, to quit;
    // returns whether there were any.
    bool quitAppId(const std::string &appId) const;

    // The application started or stopped showing in the dock.
    base::Signal<const std::string &> changed;

private:
    struct Instance {
        std::string appId;
        int64_t pid = 0;
        bool showsInDock = false;
        // Until both its properties and its process are known.
        int pending = 2;
        std::string application;
        DesktopEntryPtr entry;
        base::dbus::Proxy proxy;
        base::dbus::Match propertiesChanged;
        std::vector<base::dbus::PendingCall> calls;
    };

    void add(const std::string &name);
    void remove(const std::string &name);
    void readProperties(Instance &instance, const base::dbus::VariantMap &properties);
    // Names it again from its app id and process, as a window of it would be.
    void resolve(const std::string &name);
    void announce(const std::string &before, const std::string &after);

    base::dbus::Connection *m_bus = nullptr;
    TaskManager &m_tasks;
    // By bus name.
    std::map<std::string, Instance> m_instances;
    std::vector<std::string> m_order;
    base::dbus::Match m_nameOwnerChanged;
    base::dbus::PendingCall m_listNames;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_RUNNING_APPLICATIONS_H_
