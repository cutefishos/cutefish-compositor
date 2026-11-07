// Copyright 2026 Anatolij <anatolijphp@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/glass_preferences.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>

#include "base/settings.h"
#include "core/main_loop.h"
#include "core/preferences.h"
#include "shell/shell_data.h"
#include "ui/theme.h"

namespace cutefish::shell {

GlassPreferences &GlassPreferences::instance()
{
    static GlassPreferences preferences(preferencesFile("shell"));
    return preferences;
}

GlassPreferences::GlassPreferences(std::string path)
    : m_path(std::move(path))
{
    // The directory is watched, so a file replaced by a rename or not
    // written yet is followed too; it must exist.
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(m_path).parent_path(), error);
    m_watch = base::FileWatch::file(m_path, [this] { reload(); }, core::mainLoop());
    reload();
}

void GlassPreferences::reload()
{
    const base::Settings settings(m_path);
    // Out of range is clamped; not a number is the default.
    const auto read = [&settings](const char *key, double fallback) {
        const double value = settings.doubleValue(key, fallback);
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : fallback;
    };
    const double tint = read("Appearance/GlassTint", kDefaultTint);
    const double blur = read("Appearance/GlassBlur", kDefaultBlur);
    const bool frosted = settings.boolValue("Appearance/GlassFrosted", false);
    if (tint == m_tint && blur == m_blur && frosted == m_frosted)
        return;
    m_tint = tint;
    m_blur = blur;
    m_frosted = frosted;
    ++m_generation;
    changed();
}

GlassKey currentGlassKey()
{
    return {ui::Theme::generation(), GlassPreferences::instance().generation()};
}

render::GlassParams shellGlass(GlassRole role)
{
    const GlassPreferences &preferences = GlassPreferences::instance();
    const bool dark = ui::Theme::current().darkMode;
    if (!preferences.frosted())
        return tintedGlass(preferences.tint(), preferences.blur(), dark);
    switch (role) {
    case GlassRole::Panel:
        return frostedPanelGlass(dark);
    case GlassRole::ControlCenter:
        return frostedControlCenterGlass(dark);
    case GlassRole::Popup:
        return frostedPopupGlass(dark);
    case GlassRole::Osd:
        return frostedOsdGlass(dark);
    }
    return frostedPopupGlass(dark);
}

} // namespace cutefish::shell
