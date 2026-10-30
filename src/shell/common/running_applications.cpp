// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/common/running_applications.h"

#include <algorithm>

#include "shell/common/task_manager.h"

namespace cutefish::shell {

namespace dbus = base::dbus;

static bool isApplicationName(const std::string &name)
{
    return name.starts_with(RunningApplications::kNamePrefix)
            && name.size() > std::char_traits<char>::length(RunningApplications::kNamePrefix);
}

RunningApplications::RunningApplications(dbus::Connection *bus, TaskManager &tasks)
    : m_bus(bus)
    , m_tasks(tasks)
{
    if (!m_bus)
        return;

    dbus::MatchRule owner;
    owner.sender = "org.freedesktop.DBus";
    owner.path = "/org/freedesktop/DBus";
    owner.interface = "org.freedesktop.DBus";
    owner.member = "NameOwnerChanged";
    m_nameOwnerChanged = m_bus->subscribe(owner, [this](const dbus::Message &message) {
        std::string name, oldOwner, newOwner;
        if (!message.read(name, oldOwner, newOwner) || !isApplicationName(name))
            return;
        if (!oldOwner.empty())
            remove(name);
        if (!newOwner.empty())
            add(name);
    });

    // Those already running when the shell starts.
    const dbus::Proxy daemon(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
    m_listNames = daemon.callAsync("ListNames", [this](const dbus::Reply &reply) {
        std::vector<std::string> names;
        if (!reply.read(names))
            return;
        for (const std::string &name : names) {
            if (isApplicationName(name) && !m_instances.contains(name))
                add(name);
        }
    });

    // A scan can give a running application another entry.
    m_subscriptions.push_back(m_tasks.applicationSource().changed.connect([this] {
        for (const std::string &name : std::vector<std::string>(m_order))
            resolve(name);
    }));
}

RunningApplications::~RunningApplications() = default;

std::vector<std::string> RunningApplications::applications() const
{
    std::vector<std::string> list;
    for (const std::string &name : m_order) {
        const Instance &instance = m_instances.at(name);
        if (instance.showsInDock && !instance.application.empty()
            && std::find(list.begin(), list.end(), instance.application) == list.end())
            list.push_back(instance.application);
    }
    return list;
}

bool RunningApplications::showsInDock(const std::string &application) const
{
    return std::any_of(m_instances.begin(), m_instances.end(), [&](const auto &named) {
        return named.second.showsInDock && named.second.application == application;
    });
}

DesktopEntryPtr RunningApplications::entry(const std::string &application) const
{
    for (const auto &[name, instance] : m_instances) {
        if (instance.application == application && instance.entry)
            return instance.entry;
    }
    return nullptr;
}

bool RunningApplications::quit(const std::string &application) const
{
    if (application.empty())
        return false;
    bool found = false;
    for (const auto &[name, instance] : m_instances) {
        if (instance.application == application) {
            instance.proxy.send("Quit");
            found = true;
        }
    }
    return found;
}

void RunningApplications::add(const std::string &name)
{
    remove(name);
    Instance &instance = m_instances[name];
    m_order.push_back(name);
    instance.proxy = dbus::Proxy(m_bus, name, kPath, kInterface);

    instance.calls.push_back(instance.proxy.getAllAsync([this, name](const dbus::Reply &reply) {
        const auto it = m_instances.find(name);
        if (it == m_instances.end())
            return;
        dbus::VariantMap properties;
        reply.read(properties);
        readProperties(it->second, properties);
        if (--it->second.pending == 0)
            resolve(name);
    }));

    const dbus::Proxy daemon(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
    instance.calls.push_back(daemon.callAsync(
            "GetConnectionUnixProcessID",
            [this, name](const dbus::Reply &reply) {
                const auto it = m_instances.find(name);
                if (it == m_instances.end())
                    return;
                uint32_t pid = 0;
                if (reply.read(pid))
                    it->second.pid = pid;
                if (--it->second.pending == 0)
                    resolve(name);
            },
            name));

    instance.propertiesChanged = instance.proxy.onPropertiesChanged(
            [this, name](const dbus::VariantMap &changed, const std::vector<std::string> &) {
                const auto it = m_instances.find(name);
                if (it == m_instances.end())
                    return;
                const std::string before = it->second.showsInDock ? it->second.application : std::string();
                readProperties(it->second, changed);
                if (it->second.pending == 0)
                    resolve(name);
                else
                    announce(before, {});
            });
}

void RunningApplications::remove(const std::string &name)
{
    const auto it = m_instances.find(name);
    if (it == m_instances.end())
        return;
    const std::string before = it->second.showsInDock ? it->second.application : std::string();
    m_instances.erase(it);
    std::erase(m_order, name);
    announce(before, {});
}

void RunningApplications::readProperties(Instance &instance, const dbus::VariantMap &properties)
{
    if (const auto it = properties.find("AppId"); it != properties.end()) {
        if (const auto appId = it->second.get<std::string>())
            instance.appId = *appId;
    }
    if (const auto it = properties.find("ShowsInDock"); it != properties.end()) {
        if (const auto shows = it->second.get<bool>())
            instance.showsInDock = *shows;
    }
}

void RunningApplications::resolve(const std::string &name)
{
    const auto it = m_instances.find(name);
    if (it == m_instances.end() || it->second.pending > 0)
        return;
    Instance &instance = it->second;
    const std::string before = instance.application;
    instance.entry = instance.appId.empty() ? nullptr
                                            : m_tasks.resolveProcess(instance.appId, instance.pid, instance.appId);
    instance.application = instance.entry ? instance.entry->id : instance.appId;
    announce(before, instance.showsInDock ? instance.application : std::string());
}

void RunningApplications::announce(const std::string &before, const std::string &after)
{
    if (!before.empty())
        changed(before);
    if (!after.empty() && after != before)
        changed(after);
}

} // namespace cutefish::shell
