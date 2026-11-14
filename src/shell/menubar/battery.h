// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_BATTERY_H_
#define CUTEFISH_SHELL_MENUBAR_BATTERY_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "base/thread_pool.h"

namespace cutefish::upower {
class BatteryStatus;
} // namespace cutefish::upower

namespace cutefish::shell {

// The menu bar's battery: the framework's BatteryStatus, the icon for its
// level, and whether the percentage shows (MenuBar/BatteryPercentage in
// shell.conf).
class Battery
{
public:
    // `path` is shell.conf.
    Battery(std::shared_ptr<upower::BatteryStatus> status, std::string path);
    // cutefish-services starts after the compositor and is not activatable:
    // each time it gets an owner the status is read anew from `make`.
    void followService(base::dbus::Connection *sessionBus,
                       std::function<std::shared_ptr<upower::BatteryStatus>()> make);
    ~Battery();

    const upower::BatteryStatus &status() const { return *m_status; }

    bool isAvailable() const;
    bool onBattery() const;
    int chargePercent() const;
    const std::string &statusString() const;

    // battery-level-<0..100 in tens>[-charging][-saver]-symbolic.svg, as the
    // bar's images are named; -saver has a yellow level and no empty battery.
    std::string iconSource(bool powerSaver) const;
    static std::string iconSource(int percent, bool onBattery, bool powerSaver = false);

    bool showPercentage() const { return m_showPercentage; }
    void setShowPercentage(bool enabled);

    base::Signal<> changed;
    base::Signal<> showPercentageChanged;

private:
    void subscribe();

    std::shared_ptr<upower::BatteryStatus> m_status;
    std::function<std::shared_ptr<upower::BatteryStatus>()> m_make;
    base::dbus::NameWatch m_owner;
    std::string m_path;
    bool m_showPercentage = false;
    bool m_showPercentageSet = false;
    base::Job m_load;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_BATTERY_H_
