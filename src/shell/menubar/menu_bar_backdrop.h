// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MENU_BAR_BACKDROP_H_
#define CUTEFISH_SHELL_MENUBAR_MENU_BAR_BACKDROP_H_

#include "base/signal.h"
#include "shell/wallpaper/bar_backdrop.h"
#include "ui/geometry.h"

namespace cutefish::shell {

class Wallpaper;

// What lies behind the menu bar: the wallpaper's top strip under the bar
// decides whether the bar's contents are light or dark. The compositor's
// wallpaper judges what it shows; the bar follows that verdict, in the frame
// the picture changes.
class MenuBarBackdrop
{
public:
    using Sample = BarBackdrop::Sample;
    using Style = BarBackdrop::Style;

    MenuBarBackdrop();
    ~MenuBarBackdrop();

    MenuBarBackdrop(const MenuBarBackdrop &) = delete;
    MenuBarBackdrop &operator=(const MenuBarBackdrop &) = delete;

    // Whether a wallpaper or colour has been judged; otherwise the bar
    // follows the system's dark mode.
    bool isValid() const { return m_valid; }
    bool useLightForeground() const { return m_style.useLightForeground; }

    // Follows what the desktop shows; null stops.
    void follow(Wallpaper *wallpaper);
    // A verdict given directly.
    void present(bool valid, const Style &style);
    void setColor(const ui::Color &color);
    void setUnavailable();

    base::Signal<> changed;

    static Sample sampleImage(const ui::Bitmap &screenImage, int barHeight)
    {
        return BarBackdrop::sampleImage(screenImage, barHeight);
    }
    static Style styleForSample(const Sample &sample) { return BarBackdrop::styleForSample(sample); }

private:
    Wallpaper *m_wallpaper = nullptr;
    base::Subscription m_judgementChanged;
    bool m_valid = false;
    Style m_style;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MENU_BAR_BACKDROP_H_
