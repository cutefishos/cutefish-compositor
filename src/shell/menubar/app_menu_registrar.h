// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_APP_MENU_REGISTRAR_H_
#define CUTEFISH_SHELL_MENUBAR_APP_MENU_REGISTRAR_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "shell/common/pending_calls.h"

namespace cutefish::shell {

// com.canonical.AppMenu.Registrar at /com/canonical/AppMenu/Registrar.
// Wayland has no window id a client and the
// compositor agree on, and Qt hands out a per-process window counter, so a
// registration is the window id together with the exporter's unique bus
// name, and the menu a window owns is found through the registering
// process's id.
class AppMenuRegistrar
{
public:
    explicit AppMenuRegistrar(base::dbus::Connection *bus);
    ~AppMenuRegistrar();

    AppMenuRegistrar(const AppMenuRegistrar &) = delete;
    AppMenuRegistrar &operator=(const AppMenuRegistrar &) = delete;

    bool registerService();

    // The newest registration of the process: an application that opened a
    // second window registers again, and the last is the one in front.
    std::string serviceForPid(uint32_t pid) const;
    std::string pathForPid(uint32_t pid) const;

    // windowId, service, path
    base::Signal<uint32_t, const std::string &, const std::string &> windowRegistered;
    base::Signal<uint32_t> windowUnregistered;

private:
    struct Registration {
        uint32_t windowId = 0;
        std::string service;
        std::string path;
        uint32_t pid = 0;
    };

    void registerWindow(const std::string &sender, uint32_t windowId, const std::string &path);
    void unregisterWindow(const std::string &sender, uint32_t windowId);
    const Registration *newestFor(uint32_t pid) const;
    void removeAt(size_t index);
    void serviceUnregistered(const std::string &service);

    base::dbus::Connection *m_bus;
    std::unique_ptr<base::dbus::Object> m_object;
    PendingCalls m_calls;
    // Oldest first; the newest registration of a process wins.
    std::vector<Registration> m_registrations;
    std::map<std::string, base::dbus::NameWatch> m_watches;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_APP_MENU_REGISTRAR_H_
