// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/shell_data.h"

#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace cutefish::shell {

static std::string &dataDirectory()
{
    static std::string directory = CUTEFISH_SHELL_DATA_DIR;
    return directory;
}

std::string shellDataFile(std::string_view name)
{
    return dataDirectory() + "/" + std::string(name);
}

void setShellDataDirectoryForTesting(std::string directory)
{
    dataDirectory() = std::move(directory);
}

float glassFixed(double value)
{
    // wl_fixed_from_double: 24.8 fixed point, rounded.
    return float(std::round(value * 256.0) / 256.0);
}

// A colour component rounded to 16 bits, then back to a double.
static double colorComponent(double value)
{
    return std::round(value * 65535.0) / 65535.0;
}

render::GlassParams fishuiGlass()
{
    render::GlassParams glass;
    glass.enabled = true;
    glass.blurSigma = glassFixed(1.2);
    glass.bevel = glassFixed(14);
    glass.depth = glassFixed(16);
    glass.ior = glassFixed(1.7);
    glass.profile = glassFixed(2.5);
    glass.displacement = glassFixed(36);
    glass.chroma = glassFixed(0.05);
    glass.lightAngle = glassFixed(50);
    glass.specularIntensity = 0;
    glass.shininess = glassFixed(42);
    glass.sheen = 0;
    glass.ambientOcclusion = 0;
    glass.ambientOcclusionRadius = glassFixed(4);
    glass.saturation = glassFixed(1.0);
    glass.rim = 0;
    glass.tintRed = glassFixed(1);
    glass.tintGreen = glassFixed(1);
    glass.tintBlue = glassFixed(1);
    glass.tintAlpha = glassFixed(colorComponent(0.12));
    return glass;
}

render::GlassParams popupGlass(double red, double green, double blue, double alpha)
{
    render::GlassParams glass = fishuiGlass();
    glass.blurSigma = glassFixed(6.0);
    glass.displacement = glassFixed(20.0);
    glass.chroma = glassFixed(0.03);
    glass.saturation = glassFixed(1.2);
    glass.tintRed = glassFixed(colorComponent(red));
    glass.tintGreen = glassFixed(colorComponent(green));
    glass.tintBlue = glassFixed(colorComponent(blue));
    glass.tintAlpha = glassFixed(colorComponent(alpha));
    return glass;
}

// The dock's glass, by the GlassTint slider:
// 0 is More Clear, 1 More Tinted, 0.5 the default.
constexpr double kPace[] = {0.000, 0.048, 0.116, 0.174, 0.249, 0.314, 0.361,
                            0.398, 0.435, 0.469, 0.500, 0.587, 0.630, 0.666,
                            0.701, 0.739, 0.781, 0.827, 0.879, 0.931, 1.000};

double glassAmount(double tint)
{
    const int last = int(std::size(kPace)) - 1;
    const double x = std::clamp(tint, 0.0, 1.0) * last;
    const int i = std::min(int(std::floor(x)), last - 1);
    return kPace[i] + (x - i) * (kPace[i + 1] - kPace[i]);
}

double glassBlend(double amount, double clearValue, double defaultValue, double tintedValue)
{
    const double d0 = 2.0 * (defaultValue - clearValue);
    const double d1 = 2.0 * (tintedValue - defaultValue);
    const double middle = d0 * d1 > 0 ? 2.0 * d0 * d1 / (d0 + d1) : 0.0;
    const bool upper = amount > 0.5;
    const double t = upper ? 2.0 * amount - 1.0 : 2.0 * amount;
    const double y0 = upper ? defaultValue : clearValue;
    const double y1 = upper ? tintedValue : defaultValue;
    const double m0 = 0.5 * (upper ? middle : d0);
    const double m1 = 0.5 * (upper ? d1 : middle);
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * y0 + (t3 - 2.0 * t2 + t) * m0
            + (3.0 * t2 - 2.0 * t3) * y1 + (t3 - t2) * m1;
}

// From 0.3 to 8, the widest sigma the 30 device pixel kernel carries,
// geometrically, so 0.3 of the travel is the 0.8 the dock had.
double glassBlurSigma(double blur)
{
    return 0.3 * std::pow(8.0 / 0.3, std::clamp(blur, 0.0, 1.0));
}

render::GlassParams tintedGlass(double tint, double blur, bool)
{
    // The lens does not move with the Glass effect slider: that sets only
    // how much of the theme's background colour tints it.
    render::GlassParams glass = fishuiGlass();
    glass.blurSigma = glassFixed(glassBlurSigma(blur));
    glass.displacement = glassFixed(36.0);
    // A colour split along the lip reads as a rainbow fringe next to the rim.
    glass.chroma = 0;
    glass.saturation = glassFixed(1.35);
    // The lit rim fades with the tint: a near-solid surface has no shine.
    const double amount = glassAmount(tint);
    glass.rim = glassFixed(glassBlend(amount, 1.0, 0.6, 0.0));
    const double alpha = glassBlend(amount, 0.10, 0.35, 0.95);
    const ui::Color background = ui::Theme::current().background;
    // Rounded to 16 bits a component, then wl_fixed.
    const auto component = [](double v) { return glassFixed(colorComponent(std::clamp(v, 0.0, 1.0))); };
    glass.tintRed = component(background.r / 255.0);
    glass.tintGreen = component(background.g / 255.0);
    glass.tintBlue = component(background.b / 255.0);
    glass.tintAlpha = component(alpha);
    return glass;
}

} // namespace cutefish::shell
