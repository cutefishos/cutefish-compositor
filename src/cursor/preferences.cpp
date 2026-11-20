// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/event_loop.h"
#include "core/main_loop.h"
#include "core/preferences.h"
#include "cursor/cursor.h"
#include "cursor/preferences.h"

#include <climits>
#include <filesystem>
#include <system_error>

namespace cutefish::cursor {
Preferences::Preferences(Cursor &cursor)
    : m_cursor(cursor)
    , m_settings(preferencesFile("services"))
{
    base::EventLoop *loop = core::mainLoop();
    if (!loop)
        loop = base::EventLoop::current();
    if (loop) {
        // The directory is watched so a file written or replaced by a rename
        // is followed.
        const std::string directory = std::filesystem::path(m_settings.path()).parent_path();
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        m_watch = base::FileWatch::directory(directory, [this](const std::string &) { reload(); }, loop);
        if (base::dbus::Connection *bus = base::dbus::Connection::session(loop)) {
            for (const char *signal : {"cursorThemeChanged", "cursorSizeChanged"}) {
                base::dbus::MatchRule rule;
                rule.sender = "org.cutefish.Services";
                rule.path = "/org/cutefish/Services/Appearance";
                rule.interface = "org.cutefish.Services.Appearance";
                rule.member = signal;
                m_appearanceSignals.push_back(
                        bus->subscribe(rule, [this](const base::dbus::Message &) { reload(); }));
            }
        }
    }
    reload();
}

void Preferences::reload()
{
    m_settings.reload();
    const auto theme = m_settings.stringValue("Appearance/CursorTheme", "default");
    constexpr auto sizeKey = "Appearance/CursorSize";
    const int64_t size =
            m_settings.contains(sizeKey) ? m_settings.intValue(sizeKey, 0) : defaultSize();
    m_cursor.setTheme(theme, size >= INT_MIN && size <= INT_MAX ? int(size) : 0);
}
} // namespace cutefish::cursor
