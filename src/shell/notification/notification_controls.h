// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_CONTROLS_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_CONTROLS_H_

#include <functional>
#include <memory>
#include <string>

#include "base/signal.h"
#include "ui/geometry.h"
#include "ui/view.h"

namespace cutefish::ui {
class Label;
}

namespace cutefish::shell {

class Tween;

bool inkIsDark(ui::Color surface);
ui::Color inkOn(ui::Color surface, double alpha);
ui::Color rgba(double red, double green, double blue, double alpha);

class ClearButton : public ui::View
{
public:
    enum class Kind {
        CardClose,
        Clear,
    };

    ClearButton(ui::Window *window, Kind kind);
    ~ClearButton() override;

    // The plate's fill for hover (1) and press (2).
    void setTint(std::function<ui::Color(int state)> tint) { m_tint = std::move(tint); }

    void setConfirm(bool confirm);
    bool isArmed() const { return m_armed; }
    void setArmed(bool armed);
    // 0 is the round button, 1 the pill.
    double progress() const;
    void disarmUnless(ui::PointF windowPoint);
    void reset();

    double buttonHeight() const;
    double currentWidth() const;

    std::function<void()> onClicked;
    std::function<void()> onWidthChanged;
    std::function<void()> onArmedChanged;

    bool acceptsPointer() const override { return true; }

protected:
    void pointerEnter() override;
    void pointerLeave() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;
    void enabledChanged() override;
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    void updateLabel();

    Kind m_kind;
    std::function<ui::Color(int)> m_tint;
    bool m_confirm = false;
    bool m_armed = false;
    bool m_hovered = false;
    bool m_pressed = false;
    ui::Label *m_label = nullptr;
    std::unique_ptr<Tween> m_progress;
    base::Subscription m_languageChanged;
};

class PillButton : public ui::View
{
public:
    explicit PillButton(std::string text);

    void setText(std::string text);
    double preferredWidth() const;

    std::function<void()> onClicked;

    bool acceptsPointer() const override { return true; }

protected:
    void pointerEnter() override;
    void pointerLeave() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    bool m_hovered = false;
    bool m_pressed = false;
    ui::Label *m_label = nullptr;
};

ui::Color pillFill(ui::Color surface, bool hovered, bool pressed);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_CONTROLS_H_
