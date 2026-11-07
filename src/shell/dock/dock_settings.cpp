// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/dock/dock_settings.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "base/settings.h"
#include "shell/common/file_thread.h"

namespace cutefish::shell {

static constexpr std::chrono::milliseconds kIconSizeSaveDelay(300);

DockSettings::DockSettings(std::string path)
    : m_path(std::move(path))
{
    m_iconSizeSave.setSingleShot(true);
    m_iconSizeSave.onTimeout = [this] { write("Dock/IconSize", m_iconSize); };

    m_load = runOnFileThread([path = m_path] { return load(path); },
                             [this](Values values) {
                                 const bool iconSize = values.iconSize != m_iconSize;
                                 const bool direction = values.direction != m_direction;
                                 const bool visibility = values.visibility != m_visibility;
                                 const bool tint = values.glassTint != m_glassTint;
                                 const bool area = values.minimizedArea != m_minimizedArea;
                                 m_iconSize = values.iconSize;
                                 m_direction = values.direction;
                                 m_visibility = values.visibility;
                                 m_edgeMargins = values.edgeMargins;
                                 m_glassTint = values.glassTint;
                                 m_minimizedArea = values.minimizedArea;
                                 m_ready = true;
                                 ready();
                                 if (iconSize)
                                     iconSizeChanged();
                                 if (direction)
                                     directionChanged();
                                 if (visibility)
                                     visibilityChanged();
                                 if (tint)
                                     glassTintChanged();
                                 if (area)
                                     minimizedAreaChanged();
                             });
}

DockSettings::Values DockSettings::load(const std::string &path)
{
    base::Settings settings(path);
    // Missing keys are written with their defaults.
    if (!settings.contains("Dock/IconSize"))
        settings.setValue("Dock/IconSize", kBaseIconSize);
    if (!settings.contains("Dock/Direction"))
        settings.setValue("Dock/Direction", int(Bottom));
    if (!settings.contains("Dock/Visibility"))
        settings.setValue("Dock/Visibility", int(AlwaysShow));
    if (!settings.contains("Dock/EdgeMargins"))
        settings.setValue("Dock/EdgeMargins", 10);
    if (!settings.contains("Dock/GlassTint"))
        settings.setValue("Dock/GlassTint", 0.5);
    if (!settings.contains("Dock/MinimizedArea"))
        settings.setValue("Dock/MinimizedArea", true);
    settings.sync();

    Values values;
    values.iconSize = int(settings.intValue("Dock/IconSize", kBaseIconSize));
    values.direction = Direction(settings.intValue("Dock/Direction", Bottom));
    values.visibility = Visibility(settings.intValue("Dock/Visibility", AlwaysShow));
    values.edgeMargins = int(settings.intValue("Dock/EdgeMargins", 10));
    values.glassTint = std::clamp(settings.doubleValue("Dock/GlassTint", 0.5), 0.0, 1.0);
    values.minimizedArea = settings.boolValue("Dock/MinimizedArea", true);
    return values;
}

void DockSettings::write(const char *key, double value)
{
    const std::string name = key;
    writeSettings(m_path, [name, value](base::Settings &settings) {
        if (name == "Dock/GlassTint")
            settings.setValue(name, value);
        else
            settings.setValue(name, int64_t(value));
    });
}

void DockSettings::setIconSize(int iconSize)
{
    if (m_iconSize == iconSize)
        return;
    m_iconSize = iconSize;
    m_iconSizeSave.start(kIconSizeSaveDelay);
    iconSizeChanged();
}

void DockSettings::setDirection(Direction direction)
{
    if (m_direction == direction)
        return;
    m_direction = direction;
    write("Dock/Direction", direction);
    directionChanged();
}

void DockSettings::setVisibility(Visibility visibility)
{
    if (m_visibility == visibility)
        return;
    m_visibility = visibility;
    write("Dock/Visibility", visibility);
    visibilityChanged();
}

void DockSettings::setGlassTint(double tint)
{
    tint = std::isfinite(tint) ? std::clamp(tint, 0.0, 1.0) : 0.5;
    if (std::abs(m_glassTint - tint) < 1e-12)
        return;
    m_glassTint = tint;
    write("Dock/GlassTint", tint);
    glassTintChanged();
}

void DockSettings::setMinimizedArea(bool enabled)
{
    if (m_minimizedArea == enabled)
        return;
    m_minimizedArea = enabled;
    writeSettings(m_path, [enabled](base::Settings &settings) { settings.setValue("Dock/MinimizedArea", enabled); });
    minimizedAreaChanged();
}

} // namespace cutefish::shell
