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
    static GlassPreferences preferences;
    return preferences;
}

GlassPreferences::GlassPreferences()
    : m_path(preferencesFile("shell"))
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
    const auto read = [&settings](const char *key, double fallback) {
        const double value = settings.doubleValue(key, fallback);
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : fallback;
    };
    const double tint = read("Appearance/GlassTint", 0.5);
    const double blur = read("Appearance/GlassBlur", 0.3);
    if (std::abs(tint - m_tint) < 1e-12 && std::abs(blur - m_blur) < 1e-12)
        return;
    m_tint = tint;
    m_blur = blur;
    ++m_generation;
    changed();
}

render::GlassParams shellGlass()
{
    const GlassPreferences &preferences = GlassPreferences::instance();
    return tintedGlass(preferences.tint(), preferences.blur(), ui::Theme::current().darkMode);
}

} // namespace cutefish::shell
