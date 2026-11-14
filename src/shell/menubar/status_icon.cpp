// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/menubar/status_icon.h"

#include <cmath>

#include "shell/shell_data.h"
#include "shell/menubar/battery.h"
#include "ui/painter.h"
#include "ui/cell_layout.h"
#include "ui/window.h"

namespace cutefish::shell {

static double windowScale(const ui::View *view)
{
    const ui::Window *window = view->window();
    return window ? window->scale() : 1;
}

// The dot after the icon: six pixels, with the icon's device-pixel parity so
// both centre on the same line, centred in a slot as tall as the icon
// without rounding to whole points.
class StatusIcon::Indicator final : public ui::View
{
public:
    void setColor(ui::Color color)
    {
        m_color = color;
        requestPaint();
    }

    double size() const
    {
        const double ratio = windowScale(this);
        double pixels = std::round(6 * ratio);
        if (std::fmod(std::round(BarContext::kIconSize * ratio) - pixels, 2.0) != 0)
            pixels += 1;
        return pixels / ratio;
    }

protected:
    void paint(ui::Painter &painter) override
    {
        const double side = size();
        const ui::RectF bounds = localRect();
        const ui::RectF dot {(bounds.width - side) / 2, (bounds.height - side) / 2, side, side};
        painter.drawRoundedRect(dot, {side / 2, 0, ui::Corners::Circular}, m_color);
    }

private:
    ui::Color m_color;
};

StatusIcon::StatusIcon(BarContext *bar)
    : BarItem(bar)
{
    setInsets(BarContext::kHighlightGap / 2, BarContext::kHighlightGap / 2);
    m_content = addChild<ui::RowLayout>();
    m_content->setSpacing(BarContext::kSmallSpacing);
    m_icon = m_content->addChild<BarIcon>();
    m_indicator = m_content->addChild<Indicator>();
    m_indicator->setVisible(false);
    m_text = m_content->addChild<BarLabel>();
    m_text->setVisible(false);
    updateContent();
    updateLook();
}

void StatusIcon::setIconName(std::string name)
{
    if (name == m_iconName && m_glyph.empty())
        return;
    m_iconName = std::move(name);
    m_glyph.clear();
    updateIcon();
}

void StatusIcon::setGlyph(std::string name)
{
    if (name == m_glyph)
        return;
    m_glyph = std::move(name);
    updateIcon();
}

void StatusIcon::setIconWidth(double width)
{
    m_iconWidth = width;
    updateContent();
}

void StatusIcon::setLabel(std::string label)
{
    if (label == m_label)
        return;
    m_label = std::move(label);
    m_text->setText(m_label);
    updateContent();
}

void StatusIcon::setIndicatorColor(std::optional<ui::Color> color)
{
    if (color && color->a == 0)
        color.reset();
    if (color == m_indicatorColor)
        return;
    m_indicatorColor = color;
    if (color)
        m_indicator->setColor(*color);
    updateContent();
}

void StatusIcon::showBattery(const Battery &battery, bool percentage, bool powerSaver)
{
    setVisible(battery.isAvailable());
    std::string icon = battery.iconSource(powerSaver);
    if (icon.ends_with(".svg"))
        icon.resize(icon.size() - 4);
    setIconName(icon);
    setLabel(percentage ? std::to_string(battery.chargePercent()) + "%" : std::string());
}

void StatusIcon::showNetwork(const MenuBarIndicators::Network &network)
{
    setVisible(network.wirelessHwEnabled);
    setGlyph(!network.wirelessEnabled ? "network-wireless-disabled"
             : network.hotspotActive  ? "network-wireless-hotspot"
             : !network.wirelessName.empty() ? network.wirelessIcon
                                             : "network-wireless-connected-00");
}

void StatusIcon::updateContent()
{
    m_text->setVisible(!m_label.empty());
    m_indicator->setVisible(m_indicatorColor.has_value());
    setGlyphWidth(!m_label.empty() || m_indicatorColor ? 0 : m_iconWidth);
    ui::LayoutHints icon;
    icon.preferredWidth = m_iconWidth;
    icon.preferredHeight = BarContext::kIconSize;
    m_icon->setLayoutHints(icon);
    ui::LayoutHints dot;
    dot.preferredWidth = m_indicator->size();
    dot.preferredHeight = BarContext::kIconSize;
    m_indicator->setLayoutHints(dot);
    requestLayout();
    if (onPreferredWidthChanged)
        onPreferredWidthChanged();
}

void StatusIcon::updateIcon()
{
    if (!m_glyph.empty()) {
        m_icon->setGlyph(m_glyph);
    } else {
        const char *variant = bar()->lightContents() ? "menubar/dark/" : "menubar/light/";
        m_icon->setSource(m_iconName.empty() ? std::string() : shellDataFile(variant + m_iconName + ".svg"));
    }
}

void StatusIcon::updateLook()
{
    const bool light = bar()->lightContents();
    m_icon->setColor(bar()->textColor());
    m_icon->setDropShadow(light);
    m_text->setTextColor(bar()->textColor());
    m_text->setDropShadow(light);
    updateIcon();
    barChanged();
}

double StatusIcon::preferredWidth() const
{
    return BarContext::kItemGap + m_content->horizontalExtent().preferred + BarContext::kSmallSpacing * 2;
}

void StatusIcon::layout()
{
    // The dot's size follows the scale.
    ui::LayoutHints dot = m_indicator->layoutHints();
    if (dot.preferredWidth != m_indicator->size()) {
        dot.preferredWidth = m_indicator->size();
        m_indicator->setLayoutHints(dot);
    }
    const ui::SizeF size {m_content->horizontalExtent().preferred, m_content->verticalExtent(-1).preferred};
    m_content->setGeometry(centred(size));
}

ClockItem::ClockItem(BarContext *bar)
    : BarItem(bar)
{
    setName("clock");
    setInsets(BarContext::kHighlightGap / 2,
              BarContext::kEdgeMargin - (BarContext::kItemGap - BarContext::kHighlightGap) / 2);
    m_label = addChild<BarLabel>();
    // Figures of one width keep the clock still as the time changes.
    m_label->setFontFeatures({{"tnum", 1}});
    updateLook();
}

void ClockItem::setText(const std::string &text)
{
    if (text == m_label->text())
        return;
    const double width = m_label->sizeHint().width;
    m_label->setText(text);
    requestLayout();
    if (m_label->sizeHint().width != width && onPreferredWidthChanged)
        onPreferredWidthChanged();
}

double ClockItem::preferredWidth() const
{
    return BarContext::kItemGap / 2 + m_label->sizeHint().width + BarContext::kSmallSpacing * 2
            + BarContext::kEdgeMargin;
}

void ClockItem::updateLook()
{
    m_label->setTextColor(bar()->textColor());
    m_label->setDropShadow(bar()->lightContents());
    barChanged();
}

void ClockItem::layout()
{
    m_label->setGeometry(centred(m_label->sizeHint()));
}

StopItem::StopItem(BarContext *bar)
    : BarItem(bar)
{
    setInsets(BarContext::kHighlightGap / 2, BarContext::kHighlightGap / 2);
    setGlyphWidth(BarContext::kIconSize);
    m_icon = addChild<BarIcon>();
    m_icon->setSource(shellDataFile("menubar/recording-stop.svg"), true);
    updateLook();
}

void StopItem::updateLook()
{
    m_icon->setColor(bar()->textColor());
    m_icon->setDropShadow(bar()->lightContents());
    barChanged();
}

void StopItem::layout()
{
    m_icon->setGeometry(centred({BarContext::kIconSize, BarContext::kIconSize}));
}

} // namespace cutefish::shell
