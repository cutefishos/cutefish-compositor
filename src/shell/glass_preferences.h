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

// The [Appearance] GlassTint of shell.conf: the glass of the shell's popups,
// panels, notifications and the OSD, by the same slider the dock has for its
// own (0 More Clear, 1 More Tinted, 0.5 the default). Settings writes the
// file; the watch picks the change up.
class GlassPreferences
{
public:
    static GlassPreferences &instance();

    // 0 More Clear to 1 More Tinted.
    double tint() const { return m_tint; }
    // 0 sharpest to 1 softest: the blur under the lens (Appearance/GlassBlur).
    double blur() const { return m_blur; }
    // Counts up with every change, for those who cache the material.
    uint64_t generation() const { return m_generation; }

    base::Signal<> changed;

private:
    GlassPreferences();
    void reload();

    std::string m_path;
    double m_tint = 0.5;
    double m_blur = 0.3;
    uint64_t m_generation = 0;
    base::FileWatch m_watch;
};

// The dock's glass at the Appearance tint, in the current theme.
render::GlassParams shellGlass();

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_GLASS_PREFERENCES_H_
