// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/launchpad/launchpad_search_field.h"

#include <algorithm>
#include <cmath>

#include "base/i18n.h"
#include "ui/icon.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/path.h"

using cutefish::i18n::tr;

namespace cutefish::shell {

static const ui::Color kWhite {255, 255, 255, 255};
static constexpr double kPillScale = 0.985;
static constexpr double kRimWidth = 4.0 / 216;
static constexpr double kRimOpacity = 0.3;

// The text field itself, drawn without its frame over the pill.
class LaunchpadSearchField::Field final : public ui::TextField
{
public:
    explicit Field(LaunchpadSearchField *owner)
        : m_owner(owner)
    {
    }

protected:
    void focusIn(ui::FocusReason reason) override
    {
        ui::TextField::focusIn(reason);
        m_owner->focusChanged(true);
    }

    void focusOut() override
    {
        ui::TextField::focusOut();
        m_owner->focusChanged(false);
    }

    bool keyPress(const ui::KeyEvent &event) override
    {
        switch (event.key) {
        case ui::key::Escape:
        case ui::key::Up:
        case ui::key::Down:
        case ui::key::Return:
        case ui::key::KeypadEnter:
            // Left and right stay with the caret.
            if (m_owner->onKey && m_owner->onKey(event.key))
                return true;
            break;
        default:
            break;
        }
        return ui::TextField::keyPress(event);
    }

    void textInput(const ui::TextInputEvent &event) override
    {
        ui::TextField::textInput(event);
        // The placeholder's label goes while a preedit shows.
        m_owner->updatePlaceholder();
    }

private:
    LaunchpadSearchField *m_owner;
};

LaunchpadSearchField::LaunchpadSearchField()
{
    setName("launchpad-search");
    m_field = addChild<Field>(this);
    m_field->setFrameVisible(false);
    m_field->setTextColor(kWhite);
    m_field->setSelectByMouse(true);
    m_field->onTextChanged = [this] {
        updatePlaceholder();
        if (onTextChanged)
            onTextChanged();
    };

    m_placeholder = addChild<ui::View>();
    m_placeholder->setOpacity(0.6);
    m_glyph = m_placeholder->addChild<ui::Icon>("search");
    m_glyph->setColor(kWhite);
    m_label = m_placeholder->addChild<ui::Label>(tr("Search"));
    m_label->setColor(kWhite);
}

LaunchpadSearchField::~LaunchpadSearchField() = default;

const std::string &LaunchpadSearchField::text() const
{
    return m_field->text();
}

void LaunchpadSearchField::setText(std::string text)
{
    m_field->setText(std::move(text));
}

bool LaunchpadSearchField::hasFieldFocus() const
{
    return m_field->hasFocus();
}

void LaunchpadSearchField::focusField()
{
    m_field->setFocus();
}

void LaunchpadSearchField::retranslate()
{
    m_label->setText(tr("Search"));
    layoutPlaceholder();
}

void LaunchpadSearchField::setMetrics(const LaunchpadMetrics &metrics)
{
    if (metrics == m_metrics)
        return;
    m_metrics = metrics;
    m_field->setFontSize(metrics.labelPixelSize);
    m_label->setFontSize(metrics.labelPixelSize);
    m_glyph->setSize(metrics.searchIconSize);
    requestLayout();
}

void LaunchpadSearchField::focusChanged(bool focused)
{
    if (focused == m_focused)
        return;
    m_focused = focused;
    // Keeps the typed text on the same x as the placeholder label: the
    // margin, the glyph and the row's spacing.
    requestLayout();

    ui::Window *animating = m_animated ? window() : nullptr;
    const double progress = focused ? 1 : 0;
    m_focusTween.start(animating, m_focusProgress, progress, std::chrono::milliseconds(220), easing::outCubic,
                       [this](double value) {
                           m_focusProgress = value;
                           layoutPlaceholder();
                       });
    m_opacityTween.start(animating, m_backgroundOpacity, focused ? 0.28 : 0.18, std::chrono::milliseconds(200),
                         easing::outCubic, [this](double value) {
                             m_backgroundOpacity = value;
                             requestPaint();
                         });
    m_glyphTween.start(animating, m_glyphScale, focused ? 1.1 : 1.0, std::chrono::milliseconds(260),
                       easing::outBack, [this](double value) {
                           m_glyphScale = value;
                           m_glyph->setTransform({{}, value});
                       });
    requestPaint();
}

void LaunchpadSearchField::updatePlaceholder()
{
    const bool visible = m_field->text().empty() && m_field->preedit().empty();
    if (visible == m_label->isVisible())
        return;
    m_label->setVisible(visible);
    layoutPlaceholder();
}

void LaunchpadSearchField::layoutPlaceholder()
{
    const double height = geometry().height;
    const double iconSize = m_metrics.searchIconSize;
    ui::SizeF label = m_label->sizeHint();
    // The row takes its items' widths in whole pixels.
    label.width = std::ceil(label.width);
    const double width = iconSize + (m_label->isVisible() ? m_metrics.searchIconGap + label.width : 0);
    const double centred = (geometry().width - width) / 2;
    const double margin = 12;
    const double x = std::round(centred + (margin - centred) * m_focusProgress);
    m_placeholder->setGeometry({x, 0, width, height});
    m_glyph->setGeometry({0, (height - iconSize) / 2, iconSize, iconSize});
    m_label->setGeometry({iconSize + m_metrics.searchIconGap, (height - label.height) / 2, label.width,
                          label.height});
}

void LaunchpadSearchField::layout()
{
    const ui::RectF bounds = localRect();
    const double margin = 12;
    m_field->setTextMargins(m_focused ? margin + m_metrics.searchIconSize + m_metrics.searchIconGap : margin,
                            margin);
    m_field->setGeometry(bounds);
    layoutPlaceholder();
}

void LaunchpadSearchField::paint(ui::Painter &painter)
{
    // A pill a little inside the field, its edges on the device grid.
    const ui::RectF bounds = localRect();
    const double scale = painter.scale();
    const ui::Rect device = painter.snappedRect({(bounds.width - bounds.width * kPillScale) / 2,
                                                 (bounds.height - bounds.height * kPillScale) / 2,
                                                 bounds.width * kPillScale, bounds.height * kPillScale});
    const ui::PointF offset = painter.offset();
    const ui::RectF pill {device.x / scale - offset.x, device.y / scale - offset.y, device.width / scale,
                          device.height / scale};
    const double radius = pill.height / 2;
    ui::Path shape;
    shape.addRoundedRect(pill, radius, radius);
    painter.fillPath(shape, kWhite.withAlpha(m_backgroundOpacity));

    // The rim shows at the top and the bottom and fades out towards the sides.
    const double inset = std::max(1.0, std::round(pill.height * kRimWidth * scale)) / scale;
    const ui::RectF inner = pill.adjusted(inset, inset, -inset, -inset);
    ui::Path core;
    core.addRoundedRect(inner, radius - inset, radius - inset);
    const ui::Color edge = kWhite.withAlpha(kRimOpacity);
    const ui::Color clear = kWhite.withAlpha(0);
    const ui::LinearGradient gradient {{pill.x, pill.y},
                                       {pill.x, pill.bottom()},
                                       {{0, edge}, {0.3, clear}, {0.7, clear}, {1, edge}}};
    painter.fillPath(shape.subtracted(core), gradient);
}

} // namespace cutefish::shell
