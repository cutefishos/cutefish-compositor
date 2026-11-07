// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_DOCK_DOCK_SETTINGS_H_
#define CUTEFISH_SHELL_DOCK_DOCK_SETTINGS_H_

#include <string>

#include "base/signal.h"
#include "base/thread_pool.h"
#include "base/timer.h"

namespace cutefish::shell {

// The dock's [Dock] group of shell.conf: icon size, edge, visibility, glass
// tint and the minimized area. Missing keys are written with their
// defaults. Read on the file thread;
// the defaults stand until isReady().
class DockSettings
{
public:
    enum Direction {
        Left = 0,
        Bottom,
        Right,
    };

    enum Visibility {
        AlwaysShow = 0,
        AlwaysHide,
        IntellHide,
    };

    // The icon size the dock's metrics scale from.
    static constexpr int kBaseIconSize = 54;

    // `path` is shell.conf.
    explicit DockSettings(std::string path);

    bool isReady() const { return m_ready; }

    int iconSize() const { return m_iconSize; }
    // Written once the size has stopped changing for a moment, as a slider
    // drags it.
    void setIconSize(int iconSize);
    Direction direction() const { return m_direction; }
    void setDirection(Direction direction);
    Visibility visibility() const { return m_visibility; }
    void setVisibility(Visibility visibility);
    int edgeMargins() const { return m_edgeMargins; }
    double glassTint() const { return m_glassTint; }
    // Clamped to 0..1; not a number is the default.
    void setGlassTint(double tint);
    bool minimizedArea() const { return m_minimizedArea; }
    void setMinimizedArea(bool enabled);

    base::Signal<> ready;
    base::Signal<> iconSizeChanged;
    base::Signal<> directionChanged;
    base::Signal<> visibilityChanged;
    base::Signal<> glassTintChanged;
    base::Signal<> minimizedAreaChanged;

private:
    struct Values {
        int iconSize = kBaseIconSize;
        Direction direction = Bottom;
        Visibility visibility = AlwaysShow;
        int edgeMargins = 10;
        double glassTint = 0.5;
        bool minimizedArea = true;
    };

    static Values load(const std::string &path);
    void write(const char *key, double value);

    std::string m_path;
    bool m_ready = false;
    int m_iconSize = kBaseIconSize;
    Direction m_direction = Bottom;
    Visibility m_visibility = AlwaysShow;
    int m_edgeMargins = 10;
    double m_glassTint = 0.5;
    bool m_minimizedArea = true;
    base::Timer m_iconSizeSave;
    base::Job m_load;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_DOCK_DOCK_SETTINGS_H_
