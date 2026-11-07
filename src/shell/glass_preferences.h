// Copyright 2026 Anatolij <anatolijphp@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_GLASS_PREFERENCES_H_
#define CUTEFISH_SHELL_GLASS_PREFERENCES_H_

#include <cstdint>
#include <string>

#include "base/file_watcher.h"
#include "base/signal.h"
#include "render/glass.h"

namespace cutefish::shell {

// The [Appearance] glass of shell.conf: what the shell's panels, popups,
// notifications, OSD and tooltips, and the toolkit's windows and menus, sit
// on. GlassTint (0 More Clear, 1 More Tinted, 0.5 the default) and GlassBlur
// (0 Sharper, 1 Softer, 0.3 the default) drive tintedGlass(), the dock's
// recipe, which keeps its own pair under [Dock]. GlassFrosted (false by
// default) puts the heavy frost without a lens back, for text that must
// read over anything. Settings writes the file; the watch picks a change up
// and `changed` tells those who sent a material.
class GlassPreferences
{
public:
    static constexpr double kDefaultTint = 0.5;
    static constexpr double kDefaultBlur = 0.3;

    // The session's, from ~/.config/cutefish/shell.conf.
    static GlassPreferences &instance();
    // Read from `path` and followed while it lives; for tests.
    explicit GlassPreferences(std::string path);

    double tint() const { return m_tint; }
    double blur() const { return m_blur; }
    bool frosted() const { return m_frosted; }
    // Counts up with every change, for those who cache a material.
    uint64_t generation() const { return m_generation; }

    base::Signal<> changed;

private:
    void reload();

    std::string m_path;
    double m_tint = kDefaultTint;
    double m_blur = kDefaultBlur;
    bool m_frosted = false;
    uint64_t m_generation = 0;
    base::FileWatch m_watch;
};

// What a surface's material was made for: the theme and the preferences at
// the time. While it equals currentGlassKey() the material still holds.
struct GlassKey
{
    uint64_t theme = 0;
    uint64_t glass = 0;
    bool operator==(const GlassKey &) const = default;
};
GlassKey currentGlassKey();

// The shell's surfaces by the material they had before the sliders, which
// the frosted setting gives them back; the sliders give them all one.
enum class GlassRole {
    // The status bar's panels and the notification centre.
    Panel,
    ControlCenter,
    // Menus, drop-downs, banners, tooltips, the screenshot toolbar.
    Popup,
    // The OSD and the clipboard picker.
    Osd,
};
// The material for a surface of `role` at the current preferences and theme.
render::GlassParams shellGlass(GlassRole role);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_GLASS_PREFERENCES_H_
