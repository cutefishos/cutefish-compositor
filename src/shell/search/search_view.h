// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SEARCH_SEARCH_VIEW_H_
#define CUTEFISH_SHELL_SEARCH_SEARCH_VIEW_H_

#include <functional>
#include <vector>

#include "base/signal.h"
#include "ui/text_field.h"
#include "ui/view.h"

namespace cutefish::ui {
class Icon;
class Label;
class ListView;
} // namespace cutefish::ui

namespace cutefish::shell {

class SearchModel;

// The field's keys reach onKey before it edits with them.
class SearchFilterField : public ui::TextField
{
public:
    std::function<bool(const ui::KeyEvent &event)> onKey;

protected:
    bool keyPress(const ui::KeyEvent &event) override;
};

// The search's panel: the field, and the applications and files it found.
class SearchView : public ui::View
{
public:
    static constexpr int kWidth = 480;
    static constexpr int kPadding = 8;
    static constexpr int kHeaderHeight = 32;
    static constexpr int kBarHeight = kPadding * 2 + kHeaderHeight;
    // Matches half the collapsed bar height so the bar is a pill and keeps the
    // exact same top corners when results unfold below it.
    static constexpr double kCardRadius = kBarHeight / 2.0;
    // A row sits a padding in from the panel's edge, so this is the radius
    // that keeps it concentric with the panel's corner.
    static constexpr double kRowRadius = kCardRadius - kPadding;
    static constexpr int kSearchIconSize = 18;
    static constexpr int kSearchIconLeft = 16;
    static constexpr int kSearchIconGap = 8;
    static constexpr int kRowHeight = 44;
    static constexpr int kIconSize = 24;
    static constexpr int kPageRows = 6;
    // The panel never grows past this many rows; the list scrolls.
    static constexpr int kMaxRows = 7;

    explicit SearchView(SearchModel &model);
    ~SearchView() override;

    // As the panel opens: an empty field and the first row selected.
    void reset();
    void retranslate();
    // The bar alone until there is something to show, then one row per result.
    double preferredHeight() const;

    int currentIndex() const { return m_currentIndex; }
    void select(int index);

    // The row acted on, or the panel asked to close.
    std::function<void(int)> onActivated;
    std::function<void()> onCloseRequested;
    // Before each paint, so the window's glass follows the theme.
    std::function<void()> onPainted;
    // Something changed the height the panel wants.
    std::function<void()> onHeightChanged;

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    class Row;

    bool handleKey(const ui::KeyEvent &event);
    void modelChanged();
    void updateEmpty();
    void updateCompletion();
    // The pointer's row: it marks the row but does not scroll the list.
    void hoverRow(int row);
    void activateRow(int row);

    SearchModel &m_model;
    ui::Icon *m_searchIcon;
    SearchFilterField *m_filter;
    ui::ListView *m_list;
    ui::Label *m_empty;
    // The rest of the guessed name, drawn after the typed letters.
    ui::Label *m_completion;
    // Measures the typed text so the guess starts where it ends.
    ui::Label *m_measure;
    int m_currentIndex = 0;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SEARCH_SEARCH_VIEW_H_
