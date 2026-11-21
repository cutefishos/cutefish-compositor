// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_DECORATION_DECORATION_THEME_H_
#define CUTEFISH_DECORATION_DECORATION_THEME_H_

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <string>

#include "decoration/raster.h"
#include "decoration/shadow.h"
#include "decoration/text.h"
#include "decoration/window_visual_style.h"
#include "render/renderer.h"
#include "ui/bitmap.h"
#include "ui/window_controls.h"
#include "wm/shadow_types.h"
#include "wm/types.h"

namespace cutefish::decoration {

// Frame layout uses logical pixels; the exterior hairline uses physical pixels.
struct Metrics {
    Metrics();

    int titleBarHeight = 36;
    int borderWidth = 0;
    // Initialized in decoration_theme.cpp from the shared visual style.
    int cornerRadius;
    // Reaches outside the window.
    int resizeMargin = 5;
    int resizeCorner = 16;
    int buttonSize = int(ui::window_controls::kButton);
    int buttonSpacing = int(ui::window_controls::kSpacing);
    int buttonsMargin = (titleBarHeight - buttonSize) / 2;
    int captionMargin = 20;
    // Logical pixels: the session's FontSize points at 96 dpi.
    int textPixelSize = 12;

    ShadowStyle shadow(bool active) const
    {
        return shadowFor(active ? wm::ShadowElevation::Active : wm::ShadowElevation::Inactive);
    }

    // Fixed lobes per role, varying only strength between active and inactive,
    // so a window's scene bounds do not move on activation; sigma is capped at
    // 32 by ShadowGeometry.
    ShadowStyle shadowFor(wm::ShadowElevation elevation) const
    {
        const auto lobes = [](float wide, float narrow, int offset, float strength,
                              float contact = 1.0f) {
            return ShadowStyle {{{wide, base::Point(0, offset), 0.22f * strength},
                                 {narrow, base::Point(0, (offset + 1) / 2), 0.15f * strength},
                                 {1.5f, base::Point(0, 1), 0.16f * strength * contact}}};
        };
        switch (elevation) {
        case wm::ShadowElevation::None:
            return ShadowStyle {};
        case wm::ShadowElevation::Dock:
            return lobes(10, 4, 3, 0.25f, 0.0f);
        case wm::ShadowElevation::Popup:
            return lobes(8, 4, 3, 0.7f);
        case wm::ShadowElevation::MenuOrTooltip:
            return lobes(14, 6, 8, 0.85f);
        case wm::ShadowElevation::Inactive:
            return lobes(24, 9, 14, 0.55f);
        case wm::ShadowElevation::Active:
            return lobes(24, 9, 14, 1.0f);
        }
        return ShadowStyle {};
    }

    wm::Insets insets() const
    {
        return wm::Insets {borderWidth, titleBarHeight, borderWidth, borderWidth};
    }
};

struct Palette {
    render::Color titleBar;
    render::Color captionActive;
    render::Color captionInactive;
    // Shown in place of the backdrop under Reduce Transparency.
    render::Color opaqueBackdrop;
};

class ThemeObserver
{
public:
    virtual ~ThemeObserver() = default;

    virtual void onThemeChanged() = 0;
};

class Theme
{
public:
    Theme();
    Theme(const Theme &) = delete;
    Theme &operator=(const Theme &) = delete;
    ~Theme();

    void addObserver(ThemeObserver *observer);
    void removeObserver(ThemeObserver *observer);

    const Metrics &metrics() const { return m_metrics; }

    TextRenderer &textRenderer() { return m_textRenderer; }

    bool darkMode() const { return m_darkMode; }

    void setDarkMode(bool dark);

    // Frames re-raster their caption on the next paint, so this only notifies
    // observers.
    void setCaptionPointSize(double points);

    void setContinuousCorners(bool continuous);

    void setReduceTransparency(bool reduce);
    bool reduceTransparency() const { return m_reduceTransparency; }

    // The Appearance service's AccentColor index; only notifies observers.
    void setAccentColor(int index);
    int accentColor() const { return m_accentColor; }

    const Palette &palette() const;

    render::Color captionColor(bool active) const;
    WindowVisualStyle visualStyle() const;

    // Textures belong to the renderer that created them, so the whole cache is
    // dropped if a different one turns up.
    render::Texture *texture(render::Renderer &renderer, const std::string &key,
                             const std::function<ui::Bitmap()> &make);

    // Must run while the renderer's context is still alive.
    void clearTextures();

private:
    void notifyObservers();
    void applyOpaqueBackdrop();

    Metrics m_metrics;
    TextRenderer m_textRenderer;
    bool m_darkMode = false;
    int m_accentColor = 0;
    bool m_reduceTransparency = false;
    std::vector<ThemeObserver *> m_observers;

    render::Renderer *m_renderer = nullptr;
    std::map<std::string, std::unique_ptr<render::Texture>> m_textures;
};

} // namespace cutefish::decoration

#endif // CUTEFISH_DECORATION_DECORATION_THEME_H_
