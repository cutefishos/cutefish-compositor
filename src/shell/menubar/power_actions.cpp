// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/menubar/power_actions.h"

#include <chrono>

#include "base/logging.h"

namespace cutefish::shell {

namespace dbus = base::dbus;

static const base::LogCategory cfPower("cutefish.shell.power");

PowerActions::PowerActions(dbus::Connection *bus)
    : m_session(bus, "org.cutefish.Session", "/Session", "org.cutefish.Session")
    , m_screenSaver(bus, "org.freedesktop.ScreenSaver", "/ScreenSaver", "org.freedesktop.ScreenSaver")
{
    m_clear.setSingleShot(true);
    m_clear.onTimeout = [this] { setPending(None); };
}

void PowerActions::setPending(Action action)
{
    if (m_pending == action)
        return;
    m_pending = action;
    pendingChanged();
}

void PowerActions::request(Action action)
{
    if (action == None)
        return;
    m_clear.stop();
    setPending(action);
    requested();
}

void PowerActions::confirm()
{
    const Action action = m_pending;
    m_clear.start(std::chrono::nanoseconds(0));
    switch (action) {
    case Logout:
        logout();
        break;
    case Reboot:
        reboot();
        break;
    case Shutdown:
        shutdown();
        break;
    case None:
        break;
    }
}

void PowerActions::cancel()
{
    m_clear.start(std::chrono::nanoseconds(0));
}

// Nothing to wait for: the session first quits every application, however
// long that takes, and carries on instead if one of them keeps it.
void PowerActions::callSession(const char *method)
{
    if (!m_session.send(method))
        CF_WARNING(cfPower) << "Unable to request" << method << "from the session";
}

void PowerActions::logout()
{
    callSession("logout");
}

void PowerActions::reboot()
{
    callSession("reboot");
}

void PowerActions::shutdown()
{
    callSession("powerOff");
}

void PowerActions::suspend()
{
    m_calls.track([&](std::function<void()> finished) {
        return m_session.callAsync("suspend", [finished](const dbus::Reply &reply) {
            if (reply.isError())
                CF_WARNING(cfPower) << "Unable to request suspend:" << reply.error().message;
            finished();
        });
    });
}

void PowerActions::lockScreen()
{
    m_calls.track([&](std::function<void()> finished) {
        return m_screenSaver.callAsync("Lock", [finished](const dbus::Reply &reply) {
            if (reply.isError())
                CF_WARNING(cfPower) << "Unable to request screen lock:" << reply.error().message;
            finished();
        });
    });
}

} // namespace cutefish::shell
