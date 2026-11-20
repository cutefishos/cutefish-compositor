// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_STATUS_NOTIFIER_WATCHER_H_
#define CUTEFISH_SHELL_MENUBAR_STATUS_NOTIFIER_WATCHER_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"

namespace cutefish::shell {

class StatusNotifierWatcher
{
public:
    explicit StatusNotifierWatcher(base::dbus::Connection *bus);
    ~StatusNotifierWatcher();

    StatusNotifierWatcher(const StatusNotifierWatcher &) = delete;
    StatusNotifierWatcher &operator=(const StatusNotifierWatcher &) = delete;

    // Also takes org.kde.StatusNotifierHost-<pid>, the name the specification
    // gives a host.
    bool registerService();

    const std::vector<std::string> &items() const { return m_items; }
    bool isHostRegistered() const { return m_internalHost || !m_hosts.empty(); }

    base::Signal<const std::string &> itemRegistered;
    base::Signal<const std::string &> itemUnregistered;

private:
    void registerItem(const std::string &sender, const std::string &serviceOrPath);
    void registerHost(const std::string &service);
    void watch(const std::string &service);
    void ownerChanged(const std::string &service, const std::string &owner);
    void serviceUnregistered(const std::string &service);

    base::dbus::Connection *m_bus;
    std::unique_ptr<base::dbus::Object> m_object;
    std::string m_hostName;
    bool m_internalHost = false;
    std::vector<std::string> m_items;
    std::map<std::string, std::vector<std::string>> m_waitingItems;
    std::set<std::string> m_waitingHosts;
    std::set<std::string> m_hosts;
    std::map<std::string, base::dbus::NameWatch> m_watches;
    std::set<std::string> m_known;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_STATUS_NOTIFIER_WATCHER_H_
