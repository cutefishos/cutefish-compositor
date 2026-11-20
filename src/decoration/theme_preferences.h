// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "base/dbus.h"
#include "base/file_watcher.h"
#include "base/settings.h"

namespace cutefish::decoration {
class Theme;

// The service writes the settings file lazily, so its signals carry the
// changes.
class ThemePreferences
{
public:
    explicit ThemePreferences(Theme &theme);
    ThemePreferences(const ThemePreferences &) = delete;
    ThemePreferences &operator=(const ThemePreferences &) = delete;

    void reload();
    void setDarkMode(bool dark);
    void setContinuousCorners(bool continuous);
    void setReduceTransparency(bool reduce);
    void setColoredWindowControls(bool colored);
    void setAccentColor(int index);
    void setCaptionPointSize(double points);
    // The font size signal carries no value, so it is read back from the service.
    void reloadCaptionPointSize();

private:
    Theme &m_theme;
    base::Settings m_settings;
    base::FileWatch m_watch;
    base::dbus::Proxy m_appearance;
    std::vector<base::dbus::Match> m_signals;
    base::dbus::PendingCall m_captionPointSizeRead;
};

} // namespace cutefish::decoration
