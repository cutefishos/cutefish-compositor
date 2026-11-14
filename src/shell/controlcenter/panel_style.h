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

// One glass panel for the status panels
// and the control centre, and tiles raised off it.

// PanelStyle.popupMargin: the gap every panel keeps from the bar and the
// screen edge.
inline constexpr double kPopupMargin = 6;
// Every panel's window radius, the control centre's too.
inline constexpr double kPanelRadius = 16;
// The control centre's tiles. Concentric with the window across their 10 px
// inset would leave 6, too tight against the round plates and pills inside.
inline constexpr double kTileRadius = 12;
// A faint drop shadow under each tile, within the 10 px inset.
inline constexpr double kTileShadowSigma = 1.5;
inline constexpr double kTileShadowOffset = 1;

bool dark();
// PanelStyle.tileColor: a lift of the panel rather than a card on it.
ui::Color tileColor();
// PanelStyle.tileInkColor: what a tile reads as, opaque, for the ink of
// what is drawn on it.
ui::Color tileInkColor();
// PanelStyle.secondaryTextColor.
ui::Color secondaryTextColor();
// PanelStyle.panelTint over PanelGlass.
render::GlassParams panelGlass();
// The control centre's thinner glass: the wallpaper shows through, and the
// tiles lifted off it carry the text.
render::GlassParams controlCenterGlass();
// StatusPanel's surfaceColor: the ink reference of the status panels.
ui::Color statusSurfaceColor();
// FishUI's Theme.textColor at `opacity`, as a Rectangle's opacity draws it.
ui::Color textColor(double opacity = 1);

// Ink: WCAG relative luminance of sRGB 0-255 channels.
double relativeLuminance(double red, double green, double blue);
// White keeps at least 3:1 against the luminance.
bool prefersWhite(double luminance);
bool isDark(ui::Color background);
// White or black at `alpha`, whichever reads on the background.
ui::Color on(ui::Color background, double alpha);
// Each channel moved `amount` toward white on a dark base, black on a light one.
ui::Color shade(ui::Color base, double amount);

// The round plate of a 28 px slot snapped to whole device pixels of the
// glyph's parity, so the snapped plate and the snapped glyph share a centre
// at fractional scales (CardItem, the panels' badges).
ui::RectF parityPlate(const ui::RectF &slot, double glyph, double scale);
// TileBackground: a continuous-corner rect of the tile colour over a faint shadow.
void paintTile(ui::Painter &painter, const ui::RectF &rect);

// The menu bar's images: `light/<name>.svg` (#383838 artwork) or
// `dark/<name>.svg` (white), and the theme-less ones such as
// power-profile-*.svg, all installed under controlcenter/.
std::string artwork(std::string_view name, bool white);
std::string artwork(std::string_view name);

} // namespace cutefish::shell::panel

#endif // CUTEFISH_SHELL_CONTROLCENTER_PANEL_STYLE_H_
