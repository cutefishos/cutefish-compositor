// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_TASK_MANAGER_H_
#define CUTEFISH_SHELL_COMMON_TASK_MANAGER_H_

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/signal.h"
#include "shell/common/applications.h"
#include "shell/common/process_info.h"

namespace cutefish::shell {

class TaskWindows;

// Announces applications only after the first scan, so none is grouped under a
// bare app id before its entry was read.
class TaskManager
{
public:
    using CommandReader = std::function<ProcessCommand(int64_t pid)>;
    using ParentReader = std::function<int64_t(int64_t pid)>;

    // Both must outlive the task manager.
    TaskManager(TaskWindows &windows, Applications &applications, CommandReader commandReader = {},
                ParentReader parentReader = {});
    ~TaskManager();

    TaskManager(const TaskManager &) = delete;
    TaskManager &operator=(const TaskManager &) = delete;

    TaskWindows &taskWindows() const { return m_taskWindows; }
    Applications &applicationSource() const { return m_applications; }

    bool isReady() const { return m_ready; }

    const std::vector<std::string> &applications() const { return m_order; }
    std::vector<uint64_t> windows(const std::string &application) const;
    // Null for an application no desktop entry claims.
    DesktopEntryPtr entry(const std::string &application) const;

    // Empty for a window off the task list.
    std::string applicationOf(uint64_t window) const;
    DesktopEntryPtr entryForWindow(uint64_t window) const;

    DesktopEntryPtr resolveProcess(const std::string &appId, int64_t pid, const std::string &windowClass) const;
    DesktopEntryPtr entryForProcess(int64_t pid, const std::string &appId = {}) const;
    void claimProcess(int64_t pid, const std::string &desktopId);
    void releaseProcess(int64_t pid);

    base::Signal<> readyChanged;
    // Sent once the task list is ready.
    base::Signal<const std::string &> applicationAdded;
    base::Signal<const std::string &> applicationRemoved;
    base::Signal<const std::string &> applicationChanged;
    // Sent whether or not the task list is ready.
    base::Signal<uint64_t> windowEntryChanged;

private:
    struct Window {
        std::string appId;
        std::string title;
        int64_t pid = 0;
        std::string program;
        std::string programName;
        bool taskList = false;
        DesktopEntryPtr entry;
        std::string claimedBy;
        std::string application;
    };

    void onWindowAdded(uint64_t id);
    void onWindowChanged(uint64_t id, uint32_t changes);
    void onWindowRemoved(uint64_t id);
    void onApplicationsChanged();

    void readProcess(Window &window);
    bool resolve(uint64_t id);
    void regroup(uint64_t id, bool changed);
    void join(uint64_t id, const std::string &application);
    void leave(uint64_t id);
    void updateReady();

    TaskWindows &m_taskWindows;
    Applications &m_applications;
    CommandReader m_commandReader;
    ParentReader m_parentReader;
    std::unordered_map<uint64_t, Window> m_windows;
    std::vector<uint64_t> m_arrival;
    std::unordered_map<std::string, std::vector<uint64_t>> m_groups;
    std::vector<std::string> m_order;
    std::map<int64_t, std::string> m_claims;
    bool m_registryScanned = false;
    bool m_ready = false;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_TASK_MANAGER_H_
