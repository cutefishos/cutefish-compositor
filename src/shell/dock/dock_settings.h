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
// tint, the minimized area and magnification. Missing keys are written with their
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
    // The largest magnified size, as Settings offers it.
    static constexpr int kLargestMagnifiedIconSize = 128;

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
    // The size the icon under the pointer grows to, 0 for none, at most
    // kLargestMagnifiedIconSize; icons as large already stay as they are.
    // Written as setIconSize() writes.
    int magnifiedIconSize() const { return m_magnifiedIconSize; }
    bool magnification() const { return m_magnifiedIconSize > 0; }
    void setMagnifiedIconSize(int size);

    base::Signal<> ready;
    base::Signal<> iconSizeChanged;
    base::Signal<> directionChanged;
    base::Signal<> visibilityChanged;
    base::Signal<> glassTintChanged;
    base::Signal<> minimizedAreaChanged;
    base::Signal<> magnifiedIconSizeChanged;

private:
    struct Values {
        int iconSize = kBaseIconSize;
        Direction direction = Bottom;
        Visibility visibility = AlwaysShow;
        int edgeMargins = 10;
        double glassTint = 0.5;
        bool minimizedArea = true;
        int magnifiedIconSize = 0;
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
    int m_magnifiedIconSize = 0;
    base::Timer m_iconSizeSave;
    base::Timer m_magnifiedIconSizeSave;
    base::Job m_load;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_DOCK_DOCK_SETTINGS_H_
