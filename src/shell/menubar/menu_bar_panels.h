// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MENU_BAR_PANELS_H_
#define CUTEFISH_SHELL_MENUBAR_MENU_BAR_PANELS_H_

#include <string>

#include "base/geometry.h"
#include "base/signal.h"

namespace cutefish::shell {

class ShellWindow;

class MenuBarPanels
{
public:
    enum class Panel {
        ControlCenter,
        Network,
        Bluetooth,
        Volume,
        Battery,
        NotificationCenter,
    };

    virtual ~MenuBarPanels() = default;

    virtual bool hasPanel(Panel panel) const = 0;
    // Called after the bar closed every popup; `anchor` is in the bar window's
    // coordinates.
    virtual void openPanel(Panel panel, ShellWindow *bar, const base::Rect &anchor) = 0;
    virtual void closePanels() = 0;
    virtual bool isOpen(Panel panel) const = 0;

    base::Signal<> panelsChanged;
};

class MenuBarIndicators
{
public:
    struct Audio {
        bool hasSink = false;
        bool muted = false;
        // Over PipeWire's normal volume.
        double volume = 0;

        bool operator==(const Audio &) const = default;
    };

    struct Network {
        bool wirelessHwEnabled = false;
        bool wirelessEnabled = false;
        bool hotspotActive = false;
        // Empty when there is none.
        std::string wirelessName;
        std::string wirelessIcon;

        bool operator==(const Network &) const = default;
    };

    struct Bluetooth {
        bool available = false;
        bool enabled = false;

        bool operator==(const Bluetooth &) const = default;
    };

    virtual ~MenuBarIndicators() = default;

    virtual Audio audio() const = 0;
    virtual Network network() const = 0;
    virtual Bluetooth bluetooth() const = 0;

    base::Signal<> changed;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MENU_BAR_PANELS_H_
