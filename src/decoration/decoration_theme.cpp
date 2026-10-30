// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decoration/decoration_theme.h"

#include "render/rounded_shape.h"

#include <algorithm>

namespace cutefish::decoration {

namespace {

constexpr render::Color rgb(int r, int g, int b, float a = 1.0f)
{
    return render::Color {r / 255.0f, g / 255.0f, b / 255.0f, a};
}

// kwin-plugins: m_titleBarBgColor / m_titleBarFgColor / m_unfocusedFgColor,
// with FishUI's window border alphas.
const Palette kLight {
    rgb(255, 255, 255), // titleBar
    rgb(56, 56, 56), // captionActive   #383838
    rgb(127, 127, 127), // captionInactive #7F7F7F
};

// kwin-plugins: m_titleBarBgDarkColor / m_titleBarFgDarkColor /
// m_unfocusedFgDarkColor.
const Palette kDark {
    rgb(44, 44, 45), // titleBar        #2C2C2D
    rgb(202, 203, 206), // captionActive   #CACBCE
    rgb(112, 112, 112), // captionInactive #707070
};

} // namespace

Metrics::Metrics()
    : cornerRadius(WindowVisualStyle().radius)
{
}

Theme::Theme() = default;

Theme::~Theme() = default;

void Theme::addObserver(ThemeObserver* observer)
{
    if (observer && std::find(m_observers.begin(), m_observers.end(), observer) == m_observers.end())
        m_observers.push_back(observer);
}

void Theme::removeObserver(ThemeObserver* observer)
{
    m_observers.erase(std::remove(m_observers.begin(), m_observers.end(), observer),
                      m_observers.end());
}

void Theme::setDarkMode(bool dark)
{
    if (dark == m_darkMode)
        return;
    m_darkMode = dark;
    clearTextures();
    notifyObservers();
}

void Theme::setCaptionPointSize(double points)
{
    // Points to logical pixels at a fixed 96 dpi, as clients size text.
    const int pixels = std::clamp(base::roundToInt(points * 96.0 / 72.0), 1, 128);
    if (pixels == m_metrics.textPixelSize)
        return;
    m_metrics.textPixelSize = pixels;
    notifyObservers();
}

void Theme::setContinuousCorners(bool continuous)
{
    if (continuous == render::continuousCorners())
        return;
    render::setContinuousCorners(continuous);
    clearTextures();
    notifyObservers();
}

void Theme::setAccentColor(int index)
{
    if (index == m_accentColor)
        return;
    m_accentColor = index;
    notifyObservers();
}

void Theme::notifyObservers()
{
    // An observer may unregister while being notified.
    const auto observers = m_observers;
    for (ThemeObserver* observer : observers) {
        if (std::find(m_observers.begin(), m_observers.end(), observer) != m_observers.end())
            observer->onThemeChanged();
    }
}

const Palette& Theme::palette() const
{
    return m_darkMode ? kDark : kLight;
}

render::Color Theme::captionColor(bool active) const
{
    return active ? palette().captionActive : palette().captionInactive;
}

WindowVisualStyle Theme::visualStyle() const
{
    return WindowVisualStyle(m_darkMode, m_accentColor);
}

render::Texture* Theme::texture(render::Renderer& renderer, const std::string& key,
                                const std::function<ui::Bitmap()>& make)
{
    if (m_renderer != &renderer) {
        m_textures.clear();
        m_renderer = &renderer;
    }

    const auto existing = m_textures.find(key);
    if (existing != m_textures.end())
        return existing->second.get();

    const ui::Bitmap image = make();
    if (image.isNull())
        return nullptr;

    std::unique_ptr<render::Texture> texture = renderer.createTexture(
        image.size(), image.bits(), int(image.stride()),
        image.format() == ui::Bitmap::Format::Rgba8888Premultiplied ? render::PixelFormat::Rgba8888
                                                                    : render::PixelFormat::Bgra8888);
    render::Texture* result = texture.get();
    m_textures.emplace(key, std::move(texture));
    return result;
}

void Theme::clearTextures()
{
    m_textures.clear();
    m_renderer = nullptr;
}

} // namespace cutefish::decoration
