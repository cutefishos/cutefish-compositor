// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "auth/auth_status.h"

#include "base/dbus.h"
#include "core/main_loop.h"
#include "core/preferences.h"
#include "cutefish/powerprofiles/power_profiles.h"
#include "cutefish/upower/battery_status.h"
#include "shell/menubar/battery.h"
#include "shell/menubar/status_icon.h"
#include "shell/menubar/wireless_status.h"

namespace cutefish::auth {

namespace {

// The clock's right inset, which puts the bar's last plate 9 px from the edge.
constexpr double kLastRightInset =
        shell::BarContext::kEdgeMargin - (shell::BarContext::kItemGap - shell::BarContext::kHighlightGap) / 2;
constexpr double kInset = shell::BarContext::kHighlightGap / 2;

} // namespace

AuthStatus::AuthStatus(bool login)
{
    setName("auth-status");
    m_network = addChild<shell::StatusIcon>(this);
    m_network->setName("network");
    m_battery = addChild<shell::StatusIcon>(this);
    m_battery->setName("battery");
    m_battery->setIconWidth(kIconSize + 6);

    base::dbus::Connection *sessionBus = login ? nullptr : base::dbus::Connection::session(core::mainLoop());
    base::dbus::Connection *systemBus = base::dbus::Connection::system(core::mainLoop());
    m_batteryStatus = std::make_unique<shell::Battery>(
            std::make_shared<upower::BatteryStatus>(sessionBus, systemBus), preferencesFile("shell"));
    if (sessionBus) {
        m_batteryStatus->followService(sessionBus, [sessionBus, systemBus] {
            return std::make_shared<upower::BatteryStatus>(sessionBus, systemBus);
        });
    }
    m_powerProfiles = std::make_unique<powerprofiles::PowerProfiles>(systemBus);
    m_wirelessStatus = std::make_unique<shell::WirelessStatus>();
    m_subscriptions.push_back(m_batteryStatus->changed.connect([this] { updateBattery(); }));
    m_subscriptions.push_back(m_powerProfiles->activeProfileChanged.connect([this] { updateBattery(); }));
    m_subscriptions.push_back(m_wirelessStatus->changed.connect([this] { updateNetwork(); }));

    updateBattery();
    updateNetwork();
}

AuthStatus::~AuthStatus()
{
    m_subscriptions.clear();
}

void AuthStatus::setLightContents(bool light)
{
    if (light == m_lightContents)
        return;
    m_lightContents = light;
    m_network->updateLook();
    m_battery->updateLook();
}

void AuthStatus::updateBattery()
{
    m_battery->showBattery(*m_batteryStatus, true, m_powerProfiles->activeProfile() == "power-saver");
    requestLayout();
}

void AuthStatus::updateNetwork()
{
    m_network->showNetwork(m_wirelessStatus->network());
    requestLayout();
}

std::vector<shell::StatusIcon *> AuthStatus::shownIcons() const
{
    std::vector<shell::StatusIcon *> icons;
    for (shell::StatusIcon *icon : {m_battery, m_network}) {
        if (icon->isVisible())
            icons.push_back(icon);
    }
    return icons;
}

double AuthStatus::preferredWidth() const
{
    const std::vector<shell::StatusIcon *> icons = shownIcons();
    double width = 0;
    for (const shell::StatusIcon *icon : icons)
        width += icon->preferredWidth();
    return icons.empty() ? 0 : width + kLastRightInset - kInset;
}

void AuthStatus::layout()
{
    double right = localRect().width;
    bool last = true;
    for (shell::StatusIcon *icon : shownIcons()) {
        icon->setInsets(kInset, last ? kLastRightInset : kInset);
        const double width = icon->preferredWidth() + (last ? kLastRightInset - kInset : 0);
        right -= width;
        icon->setGeometry({right, 0, width, localRect().height});
        last = false;
    }
}

} // namespace cutefish::auth
