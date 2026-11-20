// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_SEARCH_FIELD_H_
#define CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_SEARCH_FIELD_H_

#include <functional>
#include <string>

#include "shell/launchpad/launchpad_metrics.h"
#include "shell/launchpad/launchpad_tween.h"
#include "ui/text_field.h"
#include "ui/view.h"

namespace cutefish::ui {
class Icon;
class Label;
} // namespace cutefish::ui

namespace cutefish::shell {

class LaunchpadSearchField final : public ui::View
{
public:
    LaunchpadSearchField();
    ~LaunchpadSearchField() override;

    void setMetrics(const LaunchpadMetrics &metrics);
    void setAnimated(bool animated) { m_animated = animated; }

    const std::string &text() const;
    void setText(std::string text);
    bool hasFieldFocus() const;
    void focusField();
    void retranslate();

    std::function<void()> onTextChanged;
    // True when taken.
    std::function<bool(uint32_t key)> onKey;

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    class Field;

    void focusChanged(bool focused);
    void updatePlaceholder();
    void layoutPlaceholder();

    LaunchpadMetrics m_metrics;
    Field *m_field = nullptr;
    ui::View *m_placeholder = nullptr;
    ui::Icon *m_glyph = nullptr;
    ui::Label *m_label = nullptr;
    bool m_animated = false;
    bool m_focused = false;
    double m_focusProgress = 0;
    double m_backgroundOpacity = 0.18;
    double m_glyphScale = 1;
    LaunchpadTween m_focusTween;
    LaunchpadTween m_opacityTween;
    LaunchpadTween m_glyphTween;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_SEARCH_FIELD_H_
