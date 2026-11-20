// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_STATUS_ICON_H_
#define CUTEFISH_SHELL_MENUBAR_STATUS_ICON_H_

#include <functional>
#include <optional>
#include <string>

#include "shell/menubar/bar_item.h"
#include "shell/menubar/menu_bar_panels.h"

namespace cutefish::ui {
class RowLayout;
}

namespace cutefish::shell {

class Battery;

class StatusIcon : public BarItem
{
public:
    explicit StatusIcon(BarContext *bar);

    void setIconName(std::string name);
    void setGlyph(std::string name);
    void setIconWidth(double width);
    void setLabel(std::string label);
    // None when unset.
    void setIndicatorColor(std::optional<ui::Color> color);

    void showBattery(const Battery &battery, bool percentage, bool powerSaver);
    void showNetwork(const MenuBarIndicators::Network &network);
    void showBluetooth(const MenuBarIndicators::Bluetooth &bluetooth);

    const std::string &iconName() const { return m_iconName; }
    const std::string &glyph() const { return m_glyph; }

    double preferredWidth() const;
    std::function<void()> onPreferredWidthChanged;
    void updateLook();

protected:
    void layout() override;

private:
    class Indicator;

    void updateIcon();
    void updateContent();

    std::string m_iconName;
    std::string m_glyph;
    std::string m_label;
    double m_iconWidth = BarContext::kIconSize;
    std::optional<ui::Color> m_indicatorColor;
    ui::RowLayout *m_content = nullptr;
    BarIcon *m_icon = nullptr;
    Indicator *m_indicator = nullptr;
    BarLabel *m_text = nullptr;
};

class ClockItem : public BarItem
{
public:
    explicit ClockItem(BarContext *bar);

    void setText(const std::string &text);
    double preferredWidth() const;
    void updateLook();
    std::function<void()> onPreferredWidthChanged;

protected:
    void layout() override;

private:
    BarLabel *m_label;
};

class StopItem : public BarItem
{
public:
    explicit StopItem(BarContext *bar);

    double preferredWidth() const { return BarContext::kItemGap + BarContext::kIconSize + 2 * BarContext::kSmallSpacing; }
    void updateLook();

protected:
    void layout() override;

private:
    BarIcon *m_icon;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_STATUS_ICON_H_
