// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/menubar/menu_bar_backdrop.h"

#include "shell/wallpaper/wallpaper.h"

namespace cutefish::shell {

MenuBarBackdrop::MenuBarBackdrop() = default;

MenuBarBackdrop::~MenuBarBackdrop() = default;

void MenuBarBackdrop::follow(Wallpaper *wallpaper)
{
    m_judgementChanged.reset();
    m_wallpaper = wallpaper;
    if (!m_wallpaper)
        return;
    const auto update = [this] { present(m_wallpaper->isJudged(), m_wallpaper->style()); };
    m_judgementChanged = m_wallpaper->judgementChanged.connect(update);
    update();
}

void MenuBarBackdrop::present(bool valid, const Style &style)
{
    if (valid == m_valid && style == m_style)
        return;
    m_valid = valid;
    m_style = style;
    changed();
}

void MenuBarBackdrop::setColor(const ui::Color &color)
{
    present(true, BarBackdrop::styleForSample(BarBackdrop::sampleColor(color)));
}

void MenuBarBackdrop::setUnavailable()
{
    present(false, Style());
}

} // namespace cutefish::shell
