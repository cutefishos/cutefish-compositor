// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/statusbar/battery.h"

#include "base/settings.h"
#include "cutefish/upower/battery_status.h"
#include "shell/common/file_thread.h"
#include "shell/common/service_owner.h"

namespace cutefish::shell {

Battery::Battery(std::shared_ptr<upower::BatteryStatus> status, std::string path)
    : m_status(std::move(status))
    , m_path(std::move(path))
{
    subscribe();

    m_load = runOnFileThread(
            [path = m_path] { return base::Settings(path).boolValue("Statusbar/BatteryPercentage", false); },
            [this](bool show) {
                // A choice made while the file was read stays.
                if (m_showPercentageSet || show == m_showPercentage)
                    return;
                m_showPercentage = show;
                showPercentageChanged();
            });
}

Battery::~Battery() = default;

void Battery::subscribe()
{
    m_subscriptions.clear();
    for (base::Signal<> *signal : {&m_status->availableChanged, &m_status->chargePercentChanged,
                                   &m_status->statusStringChanged, &m_status->onBatteryChanged})
        m_subscriptions.push_back(signal->connect([this] { changed(); }));
}

void Battery::followService(base::dbus::Connection *sessionBus,
                            std::function<std::shared_ptr<upower::BatteryStatus>()> make)
{
    m_make = std::move(make);
    if (!m_make)
        return;
    // A first read before the service was there failed, and a restarted
    // service may hold other values: read a fresh status each time.
    m_owner = followServiceOwner(sessionBus, "org.cutefish.Services", [this] {
        m_status = m_make();
        subscribe();
        changed();
    });
}

bool Battery::isAvailable() const
{
    return m_status->isAvailable();
}

bool Battery::onBattery() const
{
    return m_status->onBattery();
}

int Battery::chargePercent() const
{
    return m_status->chargePercent();
}

const std::string &Battery::statusString() const
{
    return m_status->statusString();
}

std::string Battery::iconSource(bool powerSaver) const
{
    return iconSource(m_status->chargePercent(), m_status->onBattery(), powerSaver);
}

std::string Battery::iconSource(int percent, bool onBattery, bool powerSaver)
{
    // 95 and up show full, then a step every ten from 5.
    int range = 0;
    if (percent >= 95)
        range = 100;
    else if (percent >= 5)
        range = (percent + 5) / 10 * 10;
    std::string name = "battery-level-" + std::to_string(range);
    if (!onBattery)
        name += "-charging";
    if (powerSaver && range > 0)
        name += "-saver";
    return name + "-symbolic.svg";
}

void Battery::setShowPercentage(bool enabled)
{
    m_showPercentageSet = true;
    if (enabled == m_showPercentage)
        return;
    m_showPercentage = enabled;
    writeSettings(m_path, [enabled](base::Settings &settings) {
        settings.setValue("Statusbar/BatteryPercentage", enabled);
    });
    showPercentageChanged();
}

} // namespace cutefish::shell
