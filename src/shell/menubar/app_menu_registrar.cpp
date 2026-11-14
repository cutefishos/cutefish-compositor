// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/menubar/app_menu_registrar.h"

#include "base/logging.h"

namespace cutefish::shell {

namespace dbus = base::dbus;

static const base::LogCategory cfAppMenu("cutefish.shell.appmenu");

static constexpr const char *kService = "com.canonical.AppMenu.Registrar";
static constexpr const char *kPath = "/com/canonical/AppMenu/Registrar";

AppMenuRegistrar::AppMenuRegistrar(dbus::Connection *bus)
    : m_bus(bus)
{
}

AppMenuRegistrar::~AppMenuRegistrar()
{
    if (m_object && m_bus)
        m_bus->releaseName(kService);
}

bool AppMenuRegistrar::registerService()
{
    if (m_object)
        return true;
    if (!m_bus)
        return false;

    auto object = std::make_unique<dbus::Object>(m_bus, kPath, kService);
    object->addMethod(
            "RegisterWindow", "uo", "",
            [this](dbus::MethodCall &call) {
                uint32_t windowId = 0;
                dbus::ObjectPath path;
                call.read(windowId, path);
                call.reply();
                registerWindow(call.message().sender(), windowId, path.path);
            },
            dbus::ArgumentNames{{"windowId", "menuObjectPath"}, {}});
    object->addMethod(
            "UnregisterWindow", "u", "",
            [this](dbus::MethodCall &call) {
                uint32_t windowId = 0;
                call.read(windowId);
                call.reply();
                unregisterWindow(call.message().sender(), windowId);
            },
            dbus::ArgumentNames{{"windowId"}, {}});
    object->addMethod(
            "GetMenuForWindow",
            [this](uint32_t windowId) {
                for (auto it = m_registrations.rbegin(); it != m_registrations.rend(); ++it) {
                    if (it->windowId == windowId)
                        return std::tuple(it->service, dbus::ObjectPath{it->path});
                }
                // An object path cannot be empty on the wire.
                return std::tuple(std::string(), dbus::ObjectPath{"/"});
            },
            dbus::ArgumentNames{{"windowId"}, {"service", "menuObjectPath"}});
    if (!object->publish() || !m_bus->requestName(kService, base::dbus::Connection::DoNotQueue)) {
        CF_WARNING(cfAppMenu) << "Cannot serve" << kService;
        return false;
    }
    m_object = std::move(object);
    return true;
}

const AppMenuRegistrar::Registration *AppMenuRegistrar::newestFor(uint32_t pid) const
{
    if (pid == 0)
        return nullptr;
    for (auto it = m_registrations.rbegin(); it != m_registrations.rend(); ++it) {
        if (it->pid == pid)
            return &*it;
    }
    return nullptr;
}

std::string AppMenuRegistrar::serviceForPid(uint32_t pid) const
{
    const Registration *registration = newestFor(pid);
    return registration ? registration->service : std::string();
}

std::string AppMenuRegistrar::pathForPid(uint32_t pid) const
{
    const Registration *registration = newestFor(pid);
    return registration ? registration->path : std::string();
}

void AppMenuRegistrar::registerWindow(const std::string &sender, uint32_t windowId, const std::string &path)
{
    // A path is what the menu needs; an empty one is bad dbusmenu use.
    if (path.empty() || sender.empty())
        return;

    for (size_t i = 0; i < m_registrations.size(); ++i) {
        if (m_registrations[i].windowId == windowId && m_registrations[i].service == sender) {
            m_registrations.erase(m_registrations.begin() + ptrdiff_t(i));
            break;
        }
    }
    m_registrations.push_back({windowId, sender, path, 0});

    if (!m_watches.count(sender)) {
        m_watches.emplace(sender, m_bus->watchName(sender, [this, sender](const std::string &owner) {
            if (owner.empty())
                serviceUnregistered(sender);
        }));
    }

    // The process id comes back from the bus; the registration is announced
    // with it in place.
    const dbus::Proxy bus(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
    m_calls.track([&](std::function<void()> finished) {
        return bus.callAsync(
                "GetConnectionUnixProcessID",
                [this, sender, windowId, path, finished](const dbus::Reply &reply) {
                    for (auto it = m_registrations.rbegin(); it != m_registrations.rend(); ++it) {
                        if (it->windowId != windowId || it->service != sender || it->path != path)
                            continue;
                        it->pid = reply.value<uint32_t>().value_or(0);
                        windowRegistered(windowId, sender, path);
                        break;
                    }
                    finished();
                },
                sender);
    });
}

void AppMenuRegistrar::unregisterWindow(const std::string &sender, uint32_t windowId)
{
    for (size_t i = m_registrations.size(); i-- > 0;) {
        const Registration &registration = m_registrations[i];
        if (registration.windowId != windowId)
            continue;
        if (!sender.empty() && registration.service != sender)
            continue;
        removeAt(i);
        windowUnregistered(windowId);
        return;
    }
}

void AppMenuRegistrar::removeAt(size_t index)
{
    const std::string service = m_registrations[index].service;
    m_registrations.erase(m_registrations.begin() + ptrdiff_t(index));
    for (const Registration &registration : m_registrations) {
        if (registration.service == service)
            return;
    }
    m_watches.erase(service);
}

void AppMenuRegistrar::serviceUnregistered(const std::string &service)
{
    for (size_t i = m_registrations.size(); i-- > 0;) {
        if (m_registrations[i].service != service)
            continue;
        const uint32_t windowId = m_registrations[i].windowId;
        m_registrations.erase(m_registrations.begin() + ptrdiff_t(i));
        windowUnregistered(windowId);
    }
    // Last: the watch is what called.
    m_watches.erase(service);
}

} // namespace cutefish::shell
