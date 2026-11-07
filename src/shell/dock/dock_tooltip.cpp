// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/dock/dock_tooltip.h"

#include <algorithm>
#include <cmath>

#include "shell/dock/dock.h"
#include "shell/dock/dock_window.h"
#include "shell/shell_data.h"
#include "shell/shell_host.h"
#include "shell/shell_window.h"
#include "ui/bubble_shape.h"
#include "ui/embedded_window.h"
#include "ui/label.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace cutefish::shell {

DockTooltip::DockTooltip(DockWindow *dock, Shell *shell)
    : m_dock(dock)
    , m_shell(shell)
{
}

DockTooltip::~DockTooltip()
{
    m_window.reset();
}

bool DockTooltip::isShown() const
{
    return m_window && !m_window->isHidden();
}

void DockTooltip::hide()
{
    if (m_window)
        m_window->hide();
}

void DockTooltip::show(const std::string &text, const base::PointF &target, const ui::PointF &offset)
{
    m_text = text;
    if (text.empty()) {
        hide();
        return;
    }
    const DockSettings::Direction direction = m_dock->dock().settings().direction();
    const bool across = direction != DockSettings::Bottom;
    const ui::Theme &theme = ui::Theme::current();
    ui::BubbleShape bubble;
    bubble.radius = theme.mediumRadius;
    bubble.continuousCorners = true;
    // Beside a side dock the text's height leaves no room for an arrow
    // between the corners, so the bubble goes without one.
    bubble.arrowEdge = across ? ui::BubbleShape::Edge::None : ui::BubbleShape::Edge::Bottom;

    // The label's size decides the bubble's, before any window is made.
    if (!m_measure) {
        m_measure = m_dock->window()->window()->contentView()->addChild<ui::Label>();
        m_measure->setElide(ui::Label::Elide::None);
        m_measure->setVisible(false);
    }
    m_measure->setText(text);
    const ui::SizeF textSize = m_measure->sizeHint();
    const double width = std::ceil(std::max(textSize.width + theme.largeSpacing * 1.5,
                                            across ? 0 : bubble.minimumEdgeLength()));
    const double height = std::ceil(textSize.height + theme.largeSpacing + (across ? 0 : bubble.arrowLength));

    // The bubble, in the dock window's coordinates: beside the dock, centred
    // on the icon, kept inside the work area.
    const base::Rect dock = m_dock->panelRect();
    const base::Rect window = m_dock->window()->geometry();
    const Shell::Output *output = m_shell->output({});
    base::Rect available = output ? m_shell->workArea(*output) : base::Rect();
    available.translate(-window.x(), -window.y());
    const double gap = theme.smallSpacing + (across ? bubble.arrowLength : 0);
    const auto within = [](double position, double size, double start, double length) {
        return std::round(std::max(start, std::min(position - size / 2, start + length - size)));
    };
    double x = 0;
    double y = 0;
    if (across) {
        x = direction == DockSettings::Left ? dock.x() + dock.width() + gap : dock.x() - gap - width;
        y = within(target.y(), height, available.y(), available.height());
    } else {
        x = within(target.x(), width, available.x(), available.width());
        y = dock.y() - gap - height;
    }
    bubble.width = width;
    bubble.height = height;
    bubble.arrowPosition = across ? target.y() - y : target.x() - x;
    x += offset.x;
    y += offset.y;
    const ui::RectF body = bubble.bodyRect();

    ui::PopupPlacement placement;
    placement.anchorRect = {x, y, 1, 1};
    placement.anchor = ui::PopupAnchor::TopLeft;
    placement.gravity = ui::PopupAnchor::BottomRight;
    placement.flipX = placement.flipY = placement.slide = false;
    if (!m_window) {
        m_window = ui::Window::createPopup(m_dock->window()->window(), placement, false);
        auto content = std::make_unique<ui::View>();
        m_label = content->addChild<ui::Label>();
        m_label->setElide(ui::Label::Elide::None);
        m_window->setContentView(std::move(content));
        m_window->resize({width, height});
    } else {
        m_window->resize({width, height});
        m_window->setPopupPlacement(placement);
    }
    m_label->setText(text);
    m_label->setGeometry({body.x + (body.width - textSize.width) / 2,
                          body.y + (body.height - textSize.height) / 2, textSize.width,
                          textSize.height});
    m_window->show();

    // The glass is the body; the arrow outside it continues the glass, and
    // the outline follows the whole bubble.
    ShellWindow *popup =
            m_shell->host()->windowFor(static_cast<ui::EmbeddedWindow *>(m_window.get()));
    if (!popup)
        return;
    const render::GlassParams glass = theme.darkMode ? popupGlass(0.17, 0.17, 0.19, 0.6)
                                                     : popupGlass(1, 1, 1, 0.6);
    const base::Rect region(int(std::floor(body.x)), int(std::floor(body.y)), int(std::ceil(body.width)),
                            int(std::ceil(body.height)));
    const double radius = std::min(bubble.radius, std::min(body.width, body.height) / 2);
    popup->setBackgroundBlur(region, int(std::lround(radius)), glass, bubble.path());
}

} // namespace cutefish::shell
