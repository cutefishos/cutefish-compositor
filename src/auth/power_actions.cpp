// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "auth/power_actions.h"

#include "base/logging.h"

namespace cutefish::auth {

namespace {

const base::LogCategory cfPower("cutefish.auth.power");

} // namespace

LogindPowerActions::LogindPowerActions(base::dbus::Connection *bus)
    : m_manager(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                "org.freedesktop.login1.Manager")
{
    refresh();
}

bool LogindPowerActions::can(Action action) const
{
    return action == Action::Restart ? m_canRestart : m_canShutDown;
}

void LogindPowerActions::refresh()
{
    query("CanReboot", m_canRestart);
    query("CanPowerOff", m_canShutDown);
}

// "yes" only: "challenge" wants a password, as when someone else is signed
// in, and nobody could give it here.
void LogindPowerActions::query(const char *method, bool &result)
{
    base::dbus::PendingCall &pending =
            &result == &m_canRestart ? m_restartQuery : m_shutDownQuery;
    pending = m_manager.callAsync(method, [this, &result](const base::dbus::Reply &reply) {
        const bool can = reply.value<std::string>().value_or("") == "yes";
        if (can == result)
            return;
        result = can;
        changed();
    });
}

void LogindPowerActions::request(Action action,
                                 std::function<void(const std::string &error)> done)
{
    const char *method = action == Action::Restart ? "Reboot" : "PowerOff";
    m_request = m_manager.callAsync(
            method,
            [method, done = std::move(done)](const base::dbus::Reply &reply) {
                if (reply.isError())
                    CF_WARNING(cfPower) << method << "refused:" << reply.error().message;
                if (done)
                    done(reply.isError() ? reply.error().message : std::string());
            },
            false);
}

} // namespace cutefish::auth
