// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_PICKER_VIEW_H_
#define CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_PICKER_VIEW_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"
#include "base/thread_pool.h"
#include "ui/bitmap.h"
#include "ui/text_field.h"
#include "ui/view.h"

namespace cutefish::ui {
class Button;
class ImageView;
class Label;
class ListView;
} // namespace cutefish::ui

namespace cutefish::shell {

class ClipboardHistory;

// The picker's keys go to onKey before the field edits with them.
class ClipboardFilterField : public ui::TextField
{
public:
    std::function<bool(const ui::KeyEvent &event)> onKey;

protected:
    bool keyPress(const ui::KeyEvent &event) override;
};

class ClipboardPickerView : public ui::View
{
public:
    static constexpr int kWidth = 720;
    static constexpr int kHeight = 440;
    static constexpr int kPadding = 12;
    static constexpr double kCardRadius = 18;
    static constexpr double kRowRadius = kCardRadius - kPadding;
    static constexpr int kHeaderHeight = 40;
    static constexpr int kRowHeight = 36;
    static constexpr int kIconSize = 24;
    static constexpr int kPageRows = 6;

    explicit ClipboardPickerView(ClipboardHistory &history);
    ~ClipboardPickerView() override;

    // As the picker opens; the pointer has to move before it selects.
    void reset();

    int currentIndex() const { return m_currentIndex; }
    // Clamped to the rows, and scrolled into sight.
    void select(int index);
    bool isConfirmingClear() const { return m_confirmingClear; }
    void setConfirmingClear(bool confirming);

    ClipboardFilterField *filterField() const { return m_filter; }
    ui::ListView *list() const { return m_list; }
    ui::Label *preview() const { return m_previewText; }
    ui::Label *emptyLabel() const { return m_empty; }

    std::function<void()> onCloseRequested;
    // Before each paint, so the window's glass follows the theme.
    std::function<void()> onPainted;

    bool pointerMoved(ui::PointF windowPosition);
    void rowClicked(int row);
    const ui::Bitmap *thumbnail(const std::string &key);

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    class Overlay;

    bool handleKey(const ui::KeyEvent &event);
    void removeCurrent();
    void modelChanged();
    void retranslate();
    void updatePreview();
    void updateEmpty();

    ClipboardHistory &m_history;
    ClipboardFilterField *m_filter;
    ui::ListView *m_list;
    ui::View *m_preview;
    ui::Label *m_previewText;
    ui::ImageView *m_previewImage;
    ui::Label *m_empty;
    Overlay *m_overlay;
    int m_currentIndex = 0;
    bool m_confirmingClear = false;
    // Where the pointer last was, in window coordinates; none since opening.
    bool m_pointerKnown = false;
    ui::PointF m_lastPointer;
    std::map<std::string, ui::Bitmap> m_thumbnails;
    std::map<std::string, base::Job> m_thumbnailJobs;
    std::string m_previewKey;
    ui::SizeF m_previewBitmapSize;
    double m_previewScale = 0;
    base::Job m_previewJob;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_PICKER_VIEW_H_
