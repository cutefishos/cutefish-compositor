// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/search/search_view.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "base/i18n.h"
#include "shell/search/search_model.h"
#include "toolkit/compositor_colors.h"
#include "ui/application.h"
#include "ui/icon.h"
#include "ui/image_view.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/list_view.h"
#include "ui/painter.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace cutefish::shell {

using cutefish::i18n::tr;

namespace {

// The line under the field, as the clipboard picker draws one.
ui::Color hairlineColor()
{
    return ui::color(toolkit::CompositorColor::ClipboardHairline);
}

// The icon a row shows: the application's own, or a folder or a file.
std::string iconFor(const SearchResult &result, double scale)
{
    std::string name;
    switch (result.kind) {
    case SearchResult::Kind::Application:
        name = result.icon.empty() ? "application-x-executable" : result.icon;
        break;
    case SearchResult::Kind::Answer:
        name = "accessories-calculator";
        break;
    case SearchResult::Kind::File:
        name = result.isDirectory ? "folder" : "text-x-generic";
        break;
    }
    return ui::themeIconPath(name, 96, scale);
}

} // namespace

bool SearchFilterField::keyPress(const ui::KeyEvent &event)
{
    if (onKey && onKey(event))
        return true;
    return ui::TextField::keyPress(event);
}

class SearchView::Row : public ui::View
{
public:
    explicit Row(SearchModel *model)
        : m_model(model)
        , m_icon(addChild<ui::ImageView>())
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

    void bind(int row, bool selected)
    {
        const std::vector<SearchResult> &results = m_model->results();
        if (row < 0 || row >= int(results.size()))
            return;
        const SearchResult &result = results[size_t(row)];
        m_selected = selected;

        const ui::Theme &theme = ui::Theme::current();
        const bool answer = result.kind == SearchResult::Kind::Answer;
        m_title->setText(result.title);
        // An answer is the panel's headline, not one more row.
        m_title->setFontSize(theme.fontSize() + (answer ? 5 : 1));
        m_title->setFontFamily(theme.fontFamily);
        m_title->setColor(selected ? ui::ColorId::HighlightedText : ui::ColorId::Text);
        m_subtitle->setText(result.subtitle);
        m_subtitle->setFontSize(theme.smallFontSize());
        m_subtitle->setFontFamily(theme.fontFamily);
        m_subtitle->setColor(selected ? ui::ColorId::HighlightedText : ui::ColorId::DisabledText);
        m_subtitle->setVisible(!result.subtitle.empty());

        const double scale = window() ? window()->scale() : 1;
        const std::string path = iconFor(result, scale);
        if (path != m_iconPath) {
            m_iconPath = path;
            m_icon->setSourceSize({double(SearchView::kIconSize), double(SearchView::kIconSize)});
            m_icon->setSource(path);
        }
        requestLayout();
        requestPaint();
    }

    bool acceptsPointer() const override { return false; }

protected:
    void layout() override
    {
        const double size = SearchView::kIconSize;
        const double left = SearchView::kPadding;
        const double width = geometry().width - 2 * left;
        const double height = geometry().height;
        const double iconY = (height - size) / 2;
        m_icon->setGeometry({left, iconY, size, size});

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
            painter.drawRoundedRect(localRect(), {SearchView::kRowRadius, 0, ui::themeCorners()},
                                    ui::Theme::current().highlight);
    }

private:
    SearchModel *m_model;
    ui::ImageView *m_icon;
    ui::Label *m_title;
    ui::Label *m_subtitle;
    std::string m_iconPath;
    bool m_selected = false;
};

SearchView::SearchView(SearchModel &model)
    : m_model(model)
    , m_filter(addChild<SearchFilterField>())
    , m_list(addChild<ui::ListView>())
    , m_empty(addChild<ui::Label>())
    , m_completion(addChild<ui::Label>())
    , m_measure(addChild<ui::Label>())
{
    m_filter->setFrameVisible(false);
    m_filter->setTextMargins(kPadding, kPadding);
    m_filter->setPlaceholder(tr("Search"));
    m_filter->setFontSize(ui::Theme::current().fontSize() + 4);
    m_filter->onKey = [this](const ui::KeyEvent &event) { return handleKey(event); };
    m_filter->onTextChanged = [this] {
        m_model.setQuery(m_filter->text());
        m_currentIndex = 0;
        m_list->setScrollPosition({0, 0});
        modelChanged();
        updateCompletion();
    };

    m_list->setClipsChildren(true);
    m_list->setElastic(false);
    m_list->setVerticalBarPolicy(ui::ScrollView::BarPolicy::AlwaysOff);
    m_list->setRowHeight(kRowHeight);
    m_list->createItem = [this] { return std::make_unique<Row>(&m_model); };
    m_list->bindItem = [this](ui::View *item, size_t row) {
        static_cast<Row *>(item)->bind(int(row), int(row) == m_currentIndex);
    };

    m_completion->setFadesWhenDisabled(false);
    m_completion->setElide(ui::Label::Elide::Right);
    m_completion->setVerticallyCentered(true);
    m_completion->setColor(ui::ColorId::DisabledText);
    m_completion->setVisible(false);
    // Transparent, not hidden: a hidden label is never laid out to measure.
    m_measure->setOpacity(0);

    m_empty->setTextAlignment(ui::Label::TextAlignment::Center);
    m_empty->setVerticallyCentered(true);
    m_empty->setFadesWhenDisabled(false);
    m_empty->setVisible(false);

    m_subscriptions.push_back(m_model.changed.connect([this] { modelChanged(); }));
    m_subscriptions.push_back(ui::Application::instance()->languageChanged.connect([this] { retranslate(); }));
    modelChanged();
}

SearchView::~SearchView() = default;

void SearchView::reset()
{
    m_filter->setText({});
    m_model.setQuery({});
    m_currentIndex = 0;
    modelChanged();
    m_list->setScrollPosition({0, 0});
    m_filter->setFocus();
}

void SearchView::retranslate()
{
    m_filter->setPlaceholder(tr("Search"));
    m_list->reload();
    updateEmpty();
}

void SearchView::select(int index)
{
    const int count = int(m_model.results().size());
    if (count == 0)
        return;
    const int clamped = std::clamp(index, 0, count - 1);
    if (clamped == m_currentIndex)
        return;
    m_currentIndex = clamped;
    m_list->reload();
    m_list->scrollToRow(size_t(clamped));
}

bool SearchView::handleKey(const ui::KeyEvent &event)
{
    switch (event.key) {
    case ui::key::Escape:
        if (!m_filter->text().empty())
            m_filter->setText({});
        else if (onCloseRequested)
            onCloseRequested();
        return true;
    case ui::key::Up:
        select(m_currentIndex - 1);
        return true;
    case ui::key::Down:
        select(m_currentIndex + 1);
        return true;
    case ui::key::PageUp:
        select(m_currentIndex - kPageRows);
        return true;
    case ui::key::PageDown:
        select(m_currentIndex + kPageRows);
        return true;
    case ui::key::Home:
        select(0);
        return true;
    case ui::key::End:
        select(int(m_model.results().size()) - 1);
        return true;
    case ui::key::Tab:
        // Complete to the selected row's name, as Spotlight fills the field.
        if (!m_model.results().empty()) {
            const SearchResult &result = m_model.results()[size_t(m_currentIndex)];
            if (result.kind != SearchResult::Kind::Answer)
                m_filter->setText(result.title);
        }
        return true;
    case ui::key::Return:
    case ui::key::KeypadEnter:
        if (!m_model.results().empty() && onActivated)
            onActivated(m_currentIndex);
        return true;
    default:
        return false;
    }
}

double SearchView::preferredHeight() const
{
    const int rows = std::min(int(m_model.results().size()), kMaxRows);
    // The bar holds the field alone; the hairline only comes with a body.
    const double bar = kPadding * 2 + kHeaderHeight;
    if (rows > 0)
        return bar + 1 + kBodyGap + rows * kRowHeight + kBodyGap;
    if (!m_filter->text().empty())
        return bar + 1 + kBodyGap + 24 + kBodyGap;
    return bar;
}

void SearchView::modelChanged()
{
    m_list->setRowCount(m_model.results().size());
    m_list->reload();
    if (m_currentIndex >= int(m_model.results().size()))
        m_currentIndex = std::max(0, int(m_model.results().size()) - 1);
    updateEmpty();
    updateCompletion();
    if (onHeightChanged)
        onHeightChanged();
}

void SearchView::updateEmpty()
{
    const bool typing = !m_filter->text().empty();
    const bool empty = typing && m_model.results().empty();
    m_empty->setText(empty ? (m_model.searching() ? tr("Searching…") : tr("No Results")) : std::string());
    m_empty->setVisible(empty);
}

void SearchView::updateCompletion()
{
    const std::string &suffix = m_model.completion();
    const bool show = !suffix.empty() && !m_filter->text().empty();
    m_completion->setVisible(show);
    if (!show) {
        m_completion->setText({});
        return;
    }

    const ui::Theme &theme = ui::Theme::current();
    const double size = theme.fontSize() + 4;
    for (ui::Label *label : {m_completion, m_measure}) {
        label->setFontSize(size);
        label->setFontFamily(theme.fontFamily);
    }
    m_completion->setText(suffix);
    m_measure->setText(m_filter->text());
    requestLayout();
}

void SearchView::layout()
{
    const double width = geometry().width;
    m_filter->setGeometry({double(kPadding), double(kPadding), width - 2 * kPadding, double(kHeaderHeight)});
    const double bar = kPadding * 2 + kHeaderHeight;
    const double bodyY = bar + 1 + kBodyGap;
    const double bodyHeight = std::max(0.0, geometry().height - bodyY - kBodyGap);
    m_list->setGeometry({double(kPadding), bodyY, width - 2 * kPadding, bodyHeight});
    m_empty->setGeometry({double(kPadding), bodyY, width - 2 * kPadding, bodyHeight});

    if (m_completion->isVisible()) {
        // The field draws its text at its left margin; the guess starts where
        // that text ends, a caret's width later.
        const double textLeft = kPadding + kPadding + m_measure->sizeHint().width + 2;
        const double room = std::max(0.0, width - kPadding - 14 - textLeft);
        m_completion->setGeometry({textLeft, double(kPadding), room, double(kHeaderHeight)});
    }
}

void SearchView::paint(ui::Painter &painter)
{
    if (onPainted)
        onPainted();
    const double bar = kPadding * 2 + kHeaderHeight;
    if (geometry().height <= bar + 1)
        return;
    const double width = geometry().width;
    painter.drawRoundedRect({double(kPadding), bar, width - 2 * kPadding, 1}, {}, hairlineColor());
}

} // namespace cutefish::shell
