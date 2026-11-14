// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_AUTH_POWER_ACTIONS_H
#define CUTEFISH_AUTH_POWER_ACTIONS_H

#include <functional>
#include <string>

#include "base/dbus.h"
#include "base/signal.h"

namespace cutefish::auth {

// What the lock screen and the greeter can do to the computer without
// anyone signing in.
class PowerActions
{
public:
    enum class Action { Restart, ShutDown };

    virtual ~PowerActions() = default;

    // Whether the action can be taken now without a password.
    virtual bool can(Action action) const = 0;
    // Asks again, for a screen about to be shown: who else is signed in
    // decides whether a password is needed.
    virtual void refresh() = 0;
    // `done` gets an empty string once the action is under way, or why it
    // was refused.
    virtual void request(Action action, std::function<void(const std::string &error)> done) = 0;

    // can() changed.
    base::Signal<> changed;
};

// logind's Reboot and PowerOff, never asking for authorization: the polkit
// agent of the locked session cannot be seen over the lock screen.
class LogindPowerActions final : public PowerActions
{
public:
    explicit LogindPowerActions(base::dbus::Connection *bus);

    bool can(Action action) const override;
    void refresh() override;
    void request(Action action, std::function<void(const std::string &error)> done) override;

private:
    void query(const char *method, bool &result);

    base::dbus::Proxy m_manager;
    bool m_canRestart = false;
    bool m_canShutDown = false;
    base::dbus::PendingCall m_restartQuery;
    base::dbus::PendingCall m_shutDownQuery;
    base::dbus::PendingCall m_request;
};

} // namespace cutefish::auth

#endif // CUTEFISH_AUTH_POWER_ACTIONS_H
