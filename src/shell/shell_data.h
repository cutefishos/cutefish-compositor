// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SHELL_DATA_H_
#define CUTEFISH_SHELL_SHELL_DATA_H_

#include <string>
#include <string_view>

#include "render/glass.h"

namespace cutefish::shell {

// The shell's own artwork, installed with the compositor.
std::string shellDataFile(std::string_view name);
// Tests read the artwork from the source tree.
void setShellDataDirectoryForTesting(std::string directory);

// FishUI's glass as a client's cutefish_surface_blur_v1 requests carry it:
// every value through wl_fixed, so the material is the one the separate
// shell's surfaces got.
float glassFixed(double value);

// FishUI's GlassEffect defaults (glassmaterial.h), enabled, before a
// component sets its own values.
render::GlassParams fishuiGlass();
// FishUI's PopupGlass: tooltips, menus, the OSD; `tint` premultiplied, from
// rgba components rounded to 16 bits each.
render::GlassParams popupGlass(double red, double green, double blue, double alpha);

// A glass tint slider's position (0 More Clear, 1 More Tinted, 0.5 the
// default) as the amount the material blends by.
double glassAmount(double tint);
// A monotone cubic through the More Clear, default and More Tinted values.
double glassBlend(double amount, double clearValue, double defaultValue, double tintedValue);
// A blur slider's position (0 Sharper, 1 Softer, 0.3 the default) as the
// factor a material's blur is scaled by: 1 at the default, a tenth at
// Sharper, tenfold at Softer.
double glassBlurFactor(double blur);
// The widest blur the glass kernel carries (30 device pixels); a larger
// sigma truncates.
constexpr double kMaxGlassBlurSigma = 8.0;
// The opacity the panels' frost had, which a tint slider reaches at More
// Tinted.
constexpr double kFrostedGlassAlpha = 0.86;

// The dock's glass, in the light or dark theme: a grey tint by a tint
// slider (0 More Clear, 1 More Tinted, 0.5 the default), the theme's own
// grey in the dark theme, the lens fading out and the blur growing toward
// More Tinted, the blur then scaled by a blur slider (glassBlurFactor). At
// the sliders' defaults, the dock as it was.
render::GlassParams tintedGlass(double tint, double blur, bool dark);
// The same without the lens and blurred to the kernel's limit, for a
// surface that must read over anything (Appearance/GlassFrosted).
render::GlassParams frostedTintedGlass(double tint, bool dark);
// The materials the shell's surfaces had before they followed the sliders,
// kept for the frosted setting: the panels' and the notification centre's
// heavy frost, the control centre's, the popups' (menus, banners, tooltips,
// the screenshot toolbar) and the OSD's (the clipboard picker's too).
render::GlassParams frostedPanelGlass(bool dark);
render::GlassParams frostedControlCenterGlass(bool dark);
render::GlassParams frostedPopupGlass(bool dark);
render::GlassParams frostedOsdGlass(bool dark);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SHELL_DATA_H_
