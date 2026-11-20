// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_TESTS_SHELL_DBUS_TEST_CLIENT_H_
#define CUTEFISH_TESTS_SHELL_DBUS_TEST_CLIENT_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "base/dbus.h"
#include "core/main_loop.h"
#include "loop_test_util.h"

namespace cutefish::test {

class DBusTestClient
{
public:
    std::unique_ptr<base::dbus::Connection> connection =
            base::dbus::Connection::open(base::dbus::Connection::Bus::Session, core::mainLoop());

    template<typename... Args>
    base::dbus::Reply call(const std::string &service, const std::string &path, const std::string &interface,
                           const std::string &method, const Args &...args)
    {
        std::optional<base::dbus::Reply> reply;
        base::dbus::Proxy proxy(connection.get(), service, path, interface);
        base::dbus::PendingCall pending = proxy.callAsync(
                method, [&reply](const base::dbus::Reply &r) { reply = r; }, args...);
        EXPECT_TRUE(runUntil([&] { return reply.has_value(); })) << method;
        return reply.value_or(base::dbus::Reply());
    }

    base::dbus::Reply getProperty(const std::string &service, const std::string &path,
                                  const std::string &interface, const std::string &name)
    {
        return call(service, path, "org.freedesktop.DBus.Properties", "Get", interface, name);
    }

    std::string introspect(const std::string &service, const std::string &path)
    {
        return call(service, path, "org.freedesktop.DBus.Introspectable", "Introspect")
                .value<std::string>()
                .value_or(std::string());
    }

    void listen(const std::string &interface)
    {
        base::dbus::MatchRule rule;
        rule.interface = interface;
        m_matches.push_back(connection->subscribe(rule, [this](const base::dbus::Message &message) {
            signalsSeen.push_back(message);
        }));
    }

    std::vector<base::dbus::Message> signalsSeen;

    // com.canonical.Unity.LauncherEntry's Update.
    void sendLauncherEntry(const std::string &uri, const base::dbus::VariantMap &properties)
    {
        EXPECT_TRUE(connection->emitSignal("/com/canonical/unity/launcherentry/1",
                                           "com.canonical.Unity.LauncherEntry", "Update", uri, properties));
    }

private:
    std::vector<base::dbus::Match> m_matches;
};

// Waits until the bus has handled what `bus` sent so far, such as the match
// rules of its subscriptions.
inline void roundTrip(base::dbus::Connection *bus)
{
    bool done = false;
    base::dbus::Proxy daemon(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
    base::dbus::PendingCall pending = daemon.callAsync("GetId", [&done](const base::dbus::Reply &) { done = true; });
    EXPECT_TRUE(runUntil([&done] { return done; }));
}

} // namespace cutefish::test

#endif // CUTEFISH_TESTS_SHELL_DBUS_TEST_CLIENT_H_
