// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/ripple/ripple_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "base/i18n.h"
#include "toolkit/compositor_colors.h"
#include "ui/icon.h"
#include "ui/image_view.h"
#include "ui/keys.h"
#include "ui/painter.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace cutefish::shell {

using cutefish::i18n::format;
using cutefish::i18n::tr;
using cutefish::i18n::trn;

namespace {

ui::Color hairlineColor()
{
    return ui::color(toolkit::CompositorColor::ClipboardHairline);
}

std::string iconForType(const std::string &type, double scale)
{
    const char *name = "computer";
    if (type == "mobile")
        name = "phone";
    else if (type == "web")
        name = "chromium-browser";
    return ui::themeIconPath(name, 96, scale);
}

std::string humanSize(unsigned long long bytes)
{
    static constexpr const char *kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double size = double(bytes);
    int unit = 0;
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        ++unit;
    }
    char text[32];
    if (unit == 0)
        std::snprintf(text, sizeof(text), "%llu B", bytes);
    else if (size >= 100)
        std::snprintf(text, sizeof(text), "%.0f %s", size, kUnits[unit]);
    else
        std::snprintf(text, sizeof(text), "%.1f %s", size, kUnits[unit]);
    return text;
}

} // namespace

class RippleView::Row : public ui::View
{
public:
    Row()
        : m_icon(addChild<ui::ImageView>())
        , m_title(addChild<ui::Label>())
        , m_subtitle(addChild<ui::Label>())
    {
        m_icon->setFillMode(ui::ImageView::FillMode::PreserveAspectFit);
        m_title->setVerticallyCentered(true);
        m_title->setElide(ui::Label::Elide::Right);
        m_title->setFadesWhenDisabled(false);
        m_subtitle->setElide(ui::Label::Elide::Right);
        m_subtitle->setFadesWhenDisabled(false);
        setTracksHover(true);
    }

    void bind(const std::string &path, const std::string &title, const std::string &subtitle,
              bool selected, int index)
    {
        m_selected = selected;
        m_index = index;
        const ui::Theme &theme = ui::Theme::current();
        m_title->setText(title);
        m_title->setFontSize(theme.fontSize() + 1);
        m_title->setFontFamily(theme.fontFamily);
        m_title->setColor(selected ? ui::ColorId::HighlightedText : ui::ColorId::Text);
        m_subtitle->setText(subtitle);
        m_subtitle->setFontSize(theme.smallFontSize());
        m_subtitle->setFontFamily(theme.fontFamily);
        m_subtitle->setColor(selected ? ui::ColorId::HighlightedText : ui::ColorId::DisabledText);
        m_subtitle->setVisible(!subtitle.empty());
        if (path != m_iconPath) {
            m_iconPath = path;
            m_icon->setSourceSize({double(RippleView::kIconSize), double(RippleView::kIconSize)});
            m_icon->setSource(path);
        }
        requestLayout();
        requestPaint();
    }

    bool acceptsPointer() const override { return true; }

    std::function<void(int)> onPressed;
    std::function<void(int)> onHovered;

    void hoverMove(const ui::PointerEvent &) override
    {
        if (onHovered)
            onHovered(m_index);
    }

    void pointerPress(const ui::PointerEvent &event) override
    {
        m_pressed = event.button == ui::MouseButton::Left;
    }

    void pointerRelease(const ui::PointerEvent &event) override
    {
        const bool clicked = m_pressed && localRect().contains(event.position);
        m_pressed = false;
        if (clicked && onPressed)
            onPressed(m_index);
    }

protected:
    void layout() override
    {
        const double size = RippleView::kIconSize;
        const double left = RippleView::kPadding;
        const double width = geometry().width - 2 * left;
        const double height = geometry().height;
        m_icon->setGeometry({left, (height - size) / 2, size, size});

        const double textLeft = left + size + 10;
        const double textWidth = std::max(0.0, width - (textLeft - left));
        const double titleHeight = m_title->sizeHint().height;
        const double subtitleHeight = m_subtitle->isVisible() ? m_subtitle->sizeHint().height : 0;
        const double textHeight = titleHeight + subtitleHeight;
        const double top = std::max(0.0, std::round((height - textHeight) / 2));
        m_title->setGeometry({textLeft, top, textWidth, titleHeight});
        m_subtitle->setGeometry({textLeft, top + titleHeight, textWidth, subtitleHeight});
    }

    void paint(ui::Painter &painter) override
    {
        if (m_selected)
            painter.drawRoundedRect(localRect(), {RippleView::kRowRadius, 0, ui::themeCorners()},
                                    ui::Theme::current().highlight);
    }

private:
    ui::ImageView *m_icon;
    ui::Label *m_title;
    ui::Label *m_subtitle;
    std::string m_iconPath;
    int m_index = 0;
    bool m_pressed = false;
    bool m_selected = false;
};

RippleView::RippleView()
    : m_title(addChild<ui::Label>())
    , m_subtitle(addChild<ui::Label>())
    , m_list(addChild<ui::ListView>())
    , m_empty(addChild<ui::Label>())
    , m_accept(addChild<ui::Button>(tr("Accept")))
    , m_decline(addChild<ui::Button>(tr("Decline")))
    , m_progress(addChild<ui::ProgressBar>())
    , m_progressText(addChild<ui::Label>())
{
    m_title->setVerticallyCentered(true);
    m_title->setElide(ui::Label::Elide::Right);
    m_title->setFadesWhenDisabled(false);
    m_subtitle->setElide(ui::Label::Elide::Right);
    m_subtitle->setFadesWhenDisabled(false);

    m_list->setClipsChildren(true);
    m_list->setElastic(false);
    m_list->setVerticalBarPolicy(ui::ScrollView::BarPolicy::AlwaysOff);
    m_list->setRowHeight(kRowHeight);
    m_list->createItem = [this] { return std::make_unique<Row>(); };
    m_list->bindItem = [this](ui::View *item, size_t row) {
        const int index = int(row);
        std::string path;
        std::string title;
        std::string subtitle;
        if (m_mode == Mode::Devices && index < int(m_devices.size())) {
            const RipplePeer &peer = m_devices[size_t(index)];
            path = iconForType(peer.type, window() ? window()->scale() : 1);
            title = peer.alias.empty() ? peer.host : peer.alias;
            subtitle = peer.type == "mobile" ? tr("Phone") : tr("Computer");
        } else if (m_mode == Mode::Incoming && index < int(m_offer.files.size())) {
            const RippleOfferFile &file = m_offer.files[size_t(index)];
            path = ui::themeIconPath("text-x-generic", 96, window() ? window()->scale() : 1);
            title = file.name;
            subtitle = humanSize(file.size);
        }
        auto *bound = static_cast<Row *>(item);
        bound->onHovered = [this](int hovered) { select(hovered); };
        bound->onPressed = [this](int pressed) {
            select(pressed);
            if (onRowActivated)
                onRowActivated(pressed);
        };
        bound->bind(path, title, subtitle, index == m_currentIndex, index);
    };

    m_empty->setTextAlignment(ui::Label::TextAlignment::Center);
    m_empty->setVerticallyCentered(true);
    m_empty->setFadesWhenDisabled(false);
    m_empty->setVisible(false);

    m_accept->onClicked = [this] {
        if (onAccept)
            onAccept();
    };
    m_decline->onClicked = [this] {
        if (onDecline)
            onDecline();
    };
    m_progress->setVisible(false);
    m_progressText->setFadesWhenDisabled(false);
    m_progressText->setVisible(false);

    rebuild();
}

RippleView::~RippleView() = default;

void RippleView::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    m_currentIndex = 0;
    rebuild();
}

void RippleView::setPending(int count)
{
    if (m_pending == count)
        return;
    m_pending = count;
    rebuild();
}

void RippleView::setDevices(std::vector<RipplePeer> devices)
{
    m_devices = std::move(devices);
    if (m_currentIndex >= int(m_devices.size()))
        m_currentIndex = std::max(0, int(m_devices.size()) - 1);
    rebuild();
}

void RippleView::setOffer(RippleOffer offer)
{
    m_offer = std::move(offer);
    m_mode = Mode::Incoming;
    m_currentIndex = 0;
    rebuild();
}

void RippleView::setTransfer(RippleTransfer transfer)
{
    m_transfer = std::move(transfer);
    m_mode = Mode::Transfer;
    rebuild();
}

void RippleView::setDone(const std::string &title, const std::string &subtitle)
{
    m_mode = Mode::Done;
    m_doneTitle = title;
    m_doneSubtitle = subtitle;
    m_currentIndex = 0;
    rebuild();
}

void RippleView::setMessage(const std::string &message)
{
    m_message = message;
    rebuild();
}

int RippleView::rowCount() const
{
    if (m_mode == Mode::Devices)
        return int(m_devices.size());
    if (m_mode == Mode::Incoming)
        return int(m_offer.files.size());
    return 0;
}

void RippleView::rebuild()
{
    const ui::Theme &theme = ui::Theme::current();
    m_title->setFontSize(theme.fontSize() + 4);
    m_title->setFontFamily(theme.fontFamily);
    m_subtitle->setFontSize(theme.smallFontSize());
    m_subtitle->setFontFamily(theme.fontFamily);
    m_subtitle->setColor(m_message.empty() ? ui::ColorId::DisabledText : ui::ColorId::Text);

    switch (m_mode) {
    case Mode::Devices:
        m_title->setText(m_pending > 0 ? tr("Send to") : tr("Ripple"));
        if (!m_message.empty())
            m_subtitle->setText(m_message);
        else if (m_pending > 0)
            m_subtitle->setText(trn("{n} file", "{n} files", uint64_t(m_pending)));
        else
            m_subtitle->setText(tr("Nearby devices"));
        break;
    case Mode::Incoming:
        m_title->setText(format(tr("Incoming from {name}"), {{"name", m_offer.sender}}));
        m_subtitle->setText(m_message.empty()
                                    ? trn("{n} file", "{n} files", uint64_t(m_offer.files.size()))
                                    : m_message);
        break;
    case Mode::Done:
        m_title->setText(m_doneTitle);
        m_subtitle->setText(m_doneSubtitle);
        break;
    case Mode::Transfer: {
        const bool sending = m_transfer.direction == "send";
        m_title->setText(sending ? tr("Sending…") : tr("Receiving…"));
        m_subtitle->setText(m_message.empty() ? m_transfer.file : m_message);
        const double value = m_transfer.total > 0 ? double(m_transfer.done) / double(m_transfer.total)
                                                  : 0.0;
        m_progress->setValue(std::clamp(value, 0.0, 1.0));
        m_progressText->setText(humanSize(m_transfer.done) + " / " + humanSize(m_transfer.total));
        break;
    }
    }

    m_list->setRowCount(size_t(std::min(rowCount(), kMaxRows)));
    m_list->reload();
    const bool incoming = m_mode == Mode::Incoming;
    m_accept->setVisible(incoming);
    m_decline->setVisible(incoming);
    m_progress->setVisible(m_mode == Mode::Transfer);
    m_progressText->setVisible(m_mode == Mode::Transfer);

    const int rows = std::min(rowCount(), kMaxRows);
    const bool empty = rows == 0 && m_mode != Mode::Transfer && m_mode != Mode::Done;
    m_list->setVisible(!empty);
    m_empty->setVisible(empty);
    if (empty) {
        if (m_mode == Mode::Devices) {
            m_empty->setText(m_message.empty() ? tr("No devices nearby") : m_message);
        } else {
            m_empty->setText(tr("No files"));
        }
    }

    requestLayout();
    if (onHeightChanged)
        onHeightChanged();
}

double RippleView::preferredHeight() const
{
    const double bar = kPadding * 2 + kHeaderHeight;
    switch (m_mode) {
    case Mode::Devices: {
        const int rows = std::max(1, std::min(rowCount(), kMaxRows));
        return bar + 1 + kBodyGap + rows * kRowHeight + kBodyGap;
    }
    case Mode::Incoming: {
        const int rows = std::max(1, std::min(rowCount(), kMaxRows));
        return bar + 1 + kBodyGap + rows * kRowHeight + kBodyGap + kButtonsHeight + kBodyGap;
    }
    case Mode::Transfer:
        return bar + 1 + kBodyGap + 44 + kBodyGap;
    case Mode::Done:
        return bar;
    }
    return bar;
}

void RippleView::select(int index)
{
    const int count = rowCount();
    if (count == 0)
        return;
    const int clamped = std::clamp(index, 0, count - 1);
    if (clamped == m_currentIndex)
        return;
    m_currentIndex = clamped;
    m_list->reload();
    m_list->scrollToRow(size_t(clamped));
}

void RippleView::layout()
{
    const double width = geometry().width;
    const double top = kPadding;
    m_title->setGeometry({double(kPadding), top, width - 2 * kPadding, double(kHeaderHeight) - 18});
    m_subtitle->setGeometry({double(kPadding), top + kHeaderHeight - 20, width - 2 * kPadding, 18});

    const double bar = kPadding * 2 + kHeaderHeight;
    const double bodyY = bar + 1 + kBodyGap;
    const double bodyHeight = std::max(0.0, geometry().height - bodyY - kBodyGap);

    if (m_mode == Mode::Incoming) {
        const double listHeight = std::max(0.0, bodyHeight - kButtonsHeight - kBodyGap);
        m_list->setGeometry({double(kPadding), bodyY, width - 2 * kPadding, listHeight});
        m_empty->setGeometry({double(kPadding), bodyY, width - 2 * kPadding, listHeight});
        const double buttonY = bodyY + listHeight + kBodyGap;
        const double buttonWidth = (width - 2 * kPadding - 8) / 2;
        m_decline->setGeometry({double(kPadding), buttonY, buttonWidth, double(kButtonsHeight)});
        m_accept->setGeometry({double(kPadding) + buttonWidth + 8, buttonY, buttonWidth,
                               double(kButtonsHeight)});
    } else if (m_mode == Mode::Transfer) {
        m_progress->setGeometry({double(kPadding), bodyY + 6, width - 2 * kPadding, 6});
        m_progressText->setGeometry({double(kPadding), bodyY + 18, width - 2 * kPadding, 22});
    } else {
        m_list->setGeometry({double(kPadding), bodyY, width - 2 * kPadding, bodyHeight});
        m_empty->setGeometry({double(kPadding), bodyY, width - 2 * kPadding, bodyHeight});
    }
}

void RippleView::paint(ui::Painter &painter)
{
    if (onPainted)
        onPainted();
    // The end of a transfer is the bar alone, as the empty search panel is.
    if (m_mode == Mode::Done)
        return;
    const double width = geometry().width;
    painter.drawRoundedRect({double(kPadding), double(kPadding * 2 + kHeaderHeight - 1),
                             width - 2 * kPadding, 1},
                            {}, hairlineColor());
}

void RippleView::pointerPress(const ui::PointerEvent &event)
{
    if (m_mode == Mode::Done && onOpenFolder)
        onOpenFolder();
    else
        ui::View::pointerPress(event);
}

bool RippleView::keyPress(const ui::KeyEvent &event)
{
    return handleKey(event);
}

bool RippleView::handleKey(const ui::KeyEvent &event)
{
    switch (event.key) {
    case ui::key::Escape:
        if (m_mode == Mode::Incoming && onDecline)
            onDecline();
        else if (onCloseRequested)
            onCloseRequested();
        return true;
    case ui::key::Up:
        select(m_currentIndex - 1);
        return true;
    case ui::key::Down:
        select(m_currentIndex + 1);
        return true;
    case ui::key::Return:
    case ui::key::KeypadEnter:
        if (m_mode == Mode::Incoming && onAccept)
            onAccept();
        else if (m_mode == Mode::Devices && onSend)
            onSend(m_currentIndex);
        else if (m_mode == Mode::Done && onOpenFolder)
            onOpenFolder();
        return true;
    default:
        return false;
    }
}

} // namespace cutefish::shell
