// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_PANEL_STYLE_H_
#define CUTEFISH_SHELL_CONTROLCENTER_PANEL_STYLE_H_

#include <string>
#include <string_view>

#include "render/glass.h"
#include "ui/geometry.h"

namespace cutefish::ui {
class Painter;
}

namespace cutefish::shell::panel {

inline constexpr double kPopupMargin = 6;
inline constexpr double kPanelRadius = 16;
inline constexpr double kTileRadius = 12;
inline constexpr double kTileShadowSigma = 1.5;
inline constexpr double kTileShadowOffset = 1;

bool dark();
ui::Color tileColor();
ui::Color tileInkColor();
ui::Color secondaryTextColor();
render::GlassParams panelGlass();
ui::Color statusSurfaceColor();
ui::Color textColor(double opacity = 1);

// WCAG relative luminance of sRGB 0-255 channels.
double relativeLuminance(double red, double green, double blue);
bool prefersWhite(double luminance);
bool isDark(ui::Color background);
ui::Color on(ui::Color background, double alpha);
ui::Color shade(ui::Color base, double amount);

ui::RectF parityPlate(const ui::RectF &slot, double glyph, double scale);
void paintTile(ui::Painter &painter, const ui::RectF &rect);

double cubicBezier(double x1, double y1, double x2, double y2, double t);
double glide(double t);

// `white` picks dark/<name>.svg, else light/<name>.svg, under controlcenter/.
std::string artwork(std::string_view name, bool white);
std::string artwork(std::string_view name);

} // namespace cutefish::shell::panel

#endif // CUTEFISH_SHELL_CONTROLCENTER_PANEL_STYLE_H_
