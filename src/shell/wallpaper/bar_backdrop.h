// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_WALLPAPER_BAR_BACKDROP_H_
#define CUTEFISH_SHELL_WALLPAPER_BAR_BACKDROP_H_

#include "ui/geometry.h"

namespace cutefish::ui {
class Bitmap;
}

namespace cutefish::shell {

// The wallpaper's strip under the
// menu bar, averaged, decides whether the bar's contents are light or dark
// and whether the desktop draws a gradient under the bar to lift them.
class BarBackdrop
{
public:
    // The menu bar's height; the sample is its strip of the wallpaper.
    static constexpr int kBarHeight = 32;
    // The gradient under the bar, three bars tall.
    static constexpr int kGradientHeight = kBarHeight * 3;

    struct Sample {
        bool valid = false;
        double red = 0;
        double green = 0;
        double blue = 0;
        // Mean grey difference between neighbouring pixels of the strip.
        double detail = 0;
    };

    struct Style {
        bool useLightForeground = false;
        bool gradientVisible = false;
        ui::Color gradientColor;

        bool operator==(const Style &) const = default;
    };

    // `screenImage` covers the screen in device pixels; `barHeight` is the
    // bar's height in the same pixels.
    static Sample sampleImage(const ui::Bitmap &screenImage, int barHeight);
    static Sample sampleColor(const ui::Color &color);
    static Style styleForSample(const Sample &sample);
    // WCAG relative luminance of sRGB components 0..255, and whether white
    // keeps a contrast of 3:1 against it.
    static double relativeLuminance(double red, double green, double blue);
    static bool prefersWhite(double luminance);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_WALLPAPER_BAR_BACKDROP_H_
