// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_TESTS_INPUT_TEST_UTIL_H_
#define CUTEFISH_TESTS_INPUT_TEST_UTIL_H_

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "base/dbus.h"
#include "base/event_loop.h"
#include "base/file.h"

namespace input_test {

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        char directory[] = "/tmp/cutefish-compositor-test-XXXXXX";
        if (mkdtemp(directory))
            m_path = directory;
    }
    ~TemporaryDirectory()
    {
        if (!m_path.empty())
            std::filesystem::remove_all(m_path);
    }

    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

    bool isValid() const { return !m_path.empty(); }
    const std::string &path() const { return m_path; }
    std::string filePath(const std::string &name) const { return m_path + "/" + name; }

private:
    std::string m_path;
};

inline bool writeFile(const std::string &path, const std::string &contents)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    return cutefish::base::file::writeAtomically(path, contents);
}

// Runs the loop until `done` holds or the time is up; whether `done` held.
inline bool runUntil(cutefish::base::EventLoop &loop, const std::function<bool()> &done,
                     std::chrono::milliseconds limit = std::chrono::seconds(5))
{
    if (done())
        return true;
    const int hook = loop.addPrepareHook([&] {
        if (done())
            loop.quit();
    });
    const int timer = loop.startTimer(limit, false, [&loop] { loop.quit(); });
    loop.run();
    loop.stopTimer(timer);
    loop.removePrepareHook(hook);
    return done();
}

// Calls a service of this process from its own connection: one on the service's connection would wait for itself.
class RemoteObject
{
public:
    RemoteObject(cutefish::base::EventLoop &loop, std::string service, std::string path,
                 std::string interface)
        : m_loop(loop)
        , m_connection(cutefish::base::dbus::Connection::open(
                  cutefish::base::dbus::Connection::Bus::Session, &loop))
        , m_proxy(m_connection.get(), service, path, std::move(interface))
        , m_introspectable(m_connection.get(), std::move(service), std::move(path),
                           "org.freedesktop.DBus.Introspectable")
    {
    }

    bool isValid() const { return m_connection != nullptr; }
    cutefish::base::dbus::Connection *connection() const { return m_connection.get(); }

    template<typename... Args>
    cutefish::base::dbus::Reply call(const std::string &method, const Args &...args)
    {
        return wait([&](auto done) { return m_proxy.callAsync(method, std::move(done), args...); });
    }

    template<typename T>
    std::optional<T> property(const std::string &name)
    {
        const cutefish::base::dbus::Reply reply =
                wait([&](auto done) { return m_proxy.getPropertyAsync(name, std::move(done)); });
        const auto value = reply.value<cutefish::base::dbus::Variant>();
        return value ? value->get<T>() : std::nullopt;
    }

    std::string introspect()
    {
        const cutefish::base::dbus::Reply reply = wait(
                [&](auto done) { return m_introspectable.callAsync("Introspect", std::move(done)); });
        return reply.value<std::string>().value_or(std::string());
    }

private:
    template<typename Start>
    cutefish::base::dbus::Reply wait(Start start)
    {
        std::optional<cutefish::base::dbus::Reply> result;
        cutefish::base::dbus::PendingCall pending =
                start([&result](const cutefish::base::dbus::Reply &reply) { result = reply; });
        runUntil(m_loop, [&] { return result.has_value(); });
        return result.value_or(cutefish::base::dbus::Reply());
    }

    cutefish::base::EventLoop &m_loop;
    std::unique_ptr<cutefish::base::dbus::Connection> m_connection;
    cutefish::base::dbus::Proxy m_proxy;
    cutefish::base::dbus::Proxy m_introspectable;
};

} // namespace input_test

#endif // CUTEFISH_TESTS_INPUT_TEST_UTIL_H_
