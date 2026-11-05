// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/statusbar/status_bar.h"

#include <filesystem>

#include "base/locale.h"
#include "base/logging.h"
#include "base/settings.h"
#include "cutefish/powerprofiles/power_profiles.h"
#include "cutefish/upower/battery_status.h"
#include "shell/common/file_thread.h"
#include "shell/statusbar/clock_format.h"

namespace cutefish::shell {

namespace dbus = base::dbus;

static const base::LogCategory cfStatusBar("cutefish.shell.statusbar");

StatusBar::StatusBar(dbus::Connection *sessionBus, dbus::Connection *systemBus, TaskManager *tasks,
                     std::string settingsPath)
    : m_bus(sessionBus)
    , m_settingsPath(std::move(settingsPath))
    , m_watcher(sessionBus)
    , m_tray(m_watcher, sessionBus, tasks)
    , m_registrar(sessionBus)
    , m_mpris(sessionBus)
    , m_battery(std::make_unique<Battery>(std::make_shared<upower::BatteryStatus>(sessionBus, systemBus),
                                          m_settingsPath))
    , m_brightness(sessionBus)
    , m_powerProfiles(std::make_unique<powerprofiles::PowerProfiles>(systemBus))
    , m_powerActions(sessionBus)
{
    m_battery->followService(sessionBus, [sessionBus, systemBus] {
        return std::make_shared<upower::BatteryStatus>(sessionBus, systemBus);
    });
    m_timeFormat = clock_format::pattern(m_timeLocale, m_twentyFourTime);

    if (m_bus) {
        dbus::MatchRule rule;
        rule.sender = "org.cutefish.Services";
        rule.path = "/org/cutefish/Services/Language";
        rule.interface = "org.cutefish.Services.Language";
        rule.member = "languageChanged";
        m_languageChanged = m_bus->subscribe(rule, [this](const dbus::Message &) { reloadLanguage(); });
    }
    reloadLanguage();
}

StatusBar::~StatusBar()
{
    if (m_object && m_bus)
        m_bus->releaseName("org.cutefish.Statusbar");
}

bool StatusBar::registerServices()
{
    if (!m_object) {
        if (!m_bus)
            return false;
        auto object = std::make_unique<dbus::Object>(m_bus, "/Statusbar", "org.cutefish.Statusbar");
        object->addMethod("setBatteryPercentage", [this](bool enabled) { m_battery->setShowPercentage(enabled); },
                          dbus::ArgumentNames{{"batteryPercentage"}, {}});
        object->addMethod("mediaPlayPause", [this] {
            if (auto *player = m_mpris.current())
                player->playPause();
        });
        object->addMethod("mediaPause", [this] {
            if (auto *player = m_mpris.current())
                player->pause();
        });
        object->addMethod("mediaPrevious", [this] {
            if (auto *player = m_mpris.current())
                player->previous();
        });
        object->addMethod("mediaNext", [this] {
            if (auto *player = m_mpris.current())
                player->next();
        });
        object->addMethod("mediaStop", [this] {
            if (auto *player = m_mpris.current())
                player->stop();
        });
        // The same as the menu's: the user is asked first. The session's
        // own methods act at once.
        object->addMethod("requestLogout", [this] { m_powerActions.request(PowerActions::Logout); });
        object->addMethod("requestReboot", [this] { m_powerActions.request(PowerActions::Reboot); });
        object->addMethod("requestShutdown", [this] { m_powerActions.request(PowerActions::Shutdown); });
        if (!object->publish() || !m_bus->requestName("org.cutefish.Statusbar", dbus::Connection::DoNotQueue)) {
            CF_WARNING(cfStatusBar) << "Cannot own org.cutefish.Statusbar";
            return false;
        }
        m_object = std::move(object);
    }
    const bool watcher = m_watcher.registerService();
    const bool registrar = m_registrar.registerService();
    return watcher && registrar;
}

namespace {

struct ClockLocale
{
    std::string name;
    bool twentyFourHour = false;
};

} // namespace

void StatusBar::reloadLanguage()
{
    // The session language and region live in cutefish-services' settings,
    // beside the shell's.
    const std::string services = (std::filesystem::path(m_settingsPath).parent_path() / "services.conf").string();
    m_language = runOnFileThread(
            [services] {
                std::string language = base::Settings(services).stringValue("Language/language");
                if (language.empty())
                    language = base::Locale::systemLanguage();
                base::RegionalPreferences preferences = base::RegionalPreferences::read(services);
                if (preferences.region.empty())
                    preferences.region = base::Locale::systemRegion();
                const base::Locale locale = base::Locale::regional(language, preferences);
                return ClockLocale {locale.formatsId(), locale.usesTwentyFourHourTime()};
            },
            [this](ClockLocale clock) {
                if (clock.name == m_timeLocale && clock.twentyFourHour == m_twentyFourTime)
                    return;
                m_timeLocale = clock.name;
                m_twentyFourTime = clock.twentyFourHour;
                updateTimeFormat();
            });
}

void StatusBar::updateTimeFormat()
{
    m_timeFormat = clock_format::pattern(m_timeLocale, m_twentyFourTime);
    timeFormatChanged();
}

} // namespace cutefish::shell
