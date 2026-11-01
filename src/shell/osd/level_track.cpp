// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/osd/level_track.h"

#include <algorithm>
#include <cmath>

#include "ui/animation.h"
#include "ui/painter.h"
#include "ui/theme.h"
#include "toolkit/compositor_colors.h"

namespace cutefish::shell {

LevelTrack::LevelTrack() = default;

LevelTrack::~LevelTrack() = default;

void LevelTrack::setPosition(double position)
{
    position = std::clamp(position, 0.0, 1.0);
    if (position == m_target)
        return;
    m_target = position;
    if (!m_animated || !window()) {
        if (m_animation)
            m_animation->stop();
        m_drawn = position;
        requestPaint();
        return;
    }
    if (!m_animation) {
        m_animation = std::make_unique<ui::Animation>(window());
        m_animation->setDuration(std::chrono::milliseconds(120));
        m_animation->setEasing(ui::Animation::Easing::OutCubic);
        m_animation->onUpdate = [this](double value) {
            m_drawn = m_from + (m_target - m_from) * value;
            requestPaint();
        };
    }
    // A retarget restarts from what is drawn.
    m_animation->stop();
    m_from = m_drawn;
    m_animation->start();
}

void LevelTrack::setSunken(bool sunken)
{
    if (sunken == m_sunken)
        return;
    m_sunken = sunken;
    requestPaint();
}

void LevelTrack::setIcon(std::string file)
{
    if (file == m_icon)
        return;
    m_icon = std::move(file);
    requestPaint();
}

void LevelTrack::paint(ui::Painter &painter)
{
    const bool dark = ui::Theme::current().darkMode;
    const ui::RectF rect = localRect();
    const double height = rect.height;
    const double travel = rect.width - height;
    // The sunken track's edges are a point wide on whole device pixels.
    const double hairline = (m_sunken ? std::max(1.0, std::round(painter.scale())) : 1) / painter.scale();
    const ui::Color outline = ui::color(m_sunken ? toolkit::CompositorColor::LevelTrackSunkenOutline
                                                 : toolkit::CompositorColor::LevelTrackOutline);
    const ui::RoundedShape pill {height / 2, 0, ui::Corners::Circular};

    painter.drawRoundedRect(rect, pill,
                            ui::color(m_sunken ? toolkit::CompositorColor::LevelTrackSunkenBackground
                                               : toolkit::CompositorColor::LevelTrackBackground),
                            hairline, outline);

    const ui::Color white = ui::Color::rgb(0xFFFFFF);
    const ui::Color fill = m_sunken ? white : ui::color(toolkit::CompositorColor::LevelTrackFill);
    const ui::RectF fillRect {0, 0, height + travel * m_drawn, height};
    painter.drawRoundedRect(fillRect, pill, fill, hairline, outline);

    // The knob, lifted by a shadow: FishUI's DropShadow of radius 3 works in
    // device pixels, a deviation of (3 dpr + 1) / 3.3333 of them.
    const ui::RectF knob {fillRect.width - height, 0, height, height};
    const double sigma = (3 * painter.scale() + 1) / 3.3333 / painter.scale();
    // The sunken knob stands out by its ring; its shadow only softens it.
    painter.drawShadow(knob, height / 2, sigma,
                       ui::color(m_sunken ? toolkit::CompositorColor::LevelTrackSunkenKnobShadow
                                          : toolkit::CompositorColor::LevelTrackKnobShadow),
                       m_sunken ? ui::PointF {0, 0} : ui::PointF {0, 1});
    painter.drawRoundedRect(knob, pill,
                            m_sunken ? white : ui::color(toolkit::CompositorColor::LevelTrackKnob), hairline,
                            m_sunken ? ui::color(toolkit::CompositorColor::LevelTrackSunkenKnobOutline) : outline);

    if (!m_icon.empty()) {
        painter.save();
        // The light artwork on a white fill.
        const bool tinted = dark && !m_sunken;
        painter.multiplyOpacity(tinted ? 0.85 : 0.7);
        const ui::RectF icon {(height - kIconSize) / 2, (height - kIconSize) / 2, kIconSize,
                              kIconSize};
        painter.drawSvg(m_icon, icon,
                        tinted ? std::optional<ui::Color>(ui::Color::rgb(0xFFFFFF)) : std::nullopt);
        painter.restore();
    }
}

} // namespace cutefish::shell
