// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decoration/theme_preferences.h"
#include "core/main_loop.h"
#include "core/preferences.h"
#include "decoration/decoration_theme.h"
#include <filesystem>

namespace cutefish::decoration {

ThemePreferences::ThemePreferences(Theme &theme)
    : m_theme(theme)
    , m_settings(preferencesFile("services"))
    , m_appearance(base::dbus::Connection::session(core::mainLoop()), "org.cutefish.Services",
                   "/org/cutefish/Services/Appearance", "org.cutefish.Services.Appearance")
{
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(m_settings.path()).parent_path(),
                                        error);
    // The directory is watched, so a file replaced by a rename is followed too.
    m_watch = base::FileWatch::file(m_settings.path(), [this] { reload(); }, core::mainLoop());
    if (m_appearance.isValid()) {
        const auto readBool = [](const base::dbus::Message &message) {
            bool value = false;
            message.read(value);
            return value;
        };
        m_signals.push_back(m_appearance.onSignal(
                "darkModeChanged", [this, readBool](const base::dbus::Message &message) {
                    setDarkMode(readBool(message));
                }));
        m_signals.push_back(m_appearance.onSignal(
                "continuousCornersChanged", [this, readBool](const base::dbus::Message &message) {
                    setContinuousCorners(readBool(message));
                }));
        m_signals.push_back(m_appearance.onSignal(
                "reduceTransparencyChanged", [this, readBool](const base::dbus::Message &message) {
                    setReduceTransparency(readBool(message));
                }));
        m_signals.push_back(m_appearance.onSignal(
                "accentColorChanged", [this](const base::dbus::Message &message) {
                    int32_t index = 0;
                    if (message.read(index))
                        setAccentColor(index);
                }));
        m_signals.push_back(m_appearance.onSignal(
                "systemFontPointSizeChanged",
                [this](const base::dbus::Message &) { reloadCaptionPointSize(); }));
    }
    reload();
}

void ThemePreferences::reload()
{
    m_settings.reload();
    setDarkMode(m_settings.boolValue("Appearance/DarkMode", false));
    setContinuousCorners(m_settings.boolValue("Appearance/ContinuousCorners", true));
    setReduceTransparency(m_settings.boolValue("Appearance/ReduceTransparency", false));
    setAccentColor(int(m_settings.intValue("Appearance/AccentColor", 0)));
    setCaptionPointSize(m_settings.doubleValue("Appearance/FontSize", 9));
}

void ThemePreferences::reloadCaptionPointSize()
{
    // Never blocking: the services daemon makes blocking calls to the compositor.
    m_captionPointSizeRead = m_appearance.getPropertyAsync(
            "systemFontPointSize", [this](const base::dbus::Reply &reply) {
                base::dbus::PendingCall done = std::move(m_captionPointSizeRead);
                const std::optional<base::dbus::Variant> points = reply.value<base::dbus::Variant>();
                if (!points) {
                    reload();
                    return;
                }
                if (const std::optional<double> value = points->get<double>())
                    setCaptionPointSize(*value);
                else if (const std::optional<int32_t> whole = points->get<int32_t>())
                    setCaptionPointSize(*whole);
                else
                    reload();
            });
}

void ThemePreferences::setCaptionPointSize(double points)
{
    if (points > 0)
        m_theme.setCaptionPointSize(points);
}

void ThemePreferences::setContinuousCorners(bool continuous)
{
    m_theme.setContinuousCorners(continuous);
}

void ThemePreferences::setReduceTransparency(bool reduce)
{
    m_theme.setReduceTransparency(reduce);
}

void ThemePreferences::setAccentColor(int index)
{
    m_theme.setAccentColor(index);
}

void ThemePreferences::setDarkMode(bool dark)
{
    m_theme.setDarkMode(dark);
}

} // namespace cutefish::decoration
