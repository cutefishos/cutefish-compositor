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
// The blur under the lens for a Blur intensity slider's position, 0 the
// sharpest to 1 the softest, as a gaussian sigma.
double glassBlurSigma(double blur);
// The dock's glass: the lens as it is, tinted with the theme's background
// colour by the Glass effect slider (0 More Clear, 1 More Tinted) and
// blurred by the Blur intensity slider, in the light or dark theme.
render::GlassParams tintedGlass(double tint, double blur, bool dark);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SHELL_DATA_H_
