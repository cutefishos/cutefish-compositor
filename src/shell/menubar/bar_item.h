// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_BAR_ITEM_H_
#define CUTEFISH_SHELL_MENUBAR_BAR_ITEM_H_

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ui/geometry.h"
#include "ui/blurred_copy.h"
#include "ui/styled_label.h"
#include "ui/view.h"

namespace cutefish::ui {
class Icon;
} // namespace cutefish::ui

namespace cutefish::shell {

class BarContext
{
public:
    static constexpr double kIconSize = 16;
    static constexpr double kFontPointSize = 10;
    static constexpr double kEdgeMargin = 12;
    static constexpr double kItemGap = 12;
    static constexpr double kHighlightGap = 6;
    static constexpr double kSmallSpacing = 6;

    virtual ~BarContext() = default;

    virtual bool lightContents() const = 0;
    ui::Color textColor() const;
    ui::Color menuTextColor() const;
    virtual void platePainted() {}
};

class BarItem : public ui::View
{
public:
    explicit BarItem(BarContext *bar);

    void setInsets(double left, double right);
    double leftInset() const { return m_leftInset; }
    double rightInset() const { return m_rightInset; }
    double centerOffset() const { return (m_leftInset - m_rightInset) / 2; }
    void setGlyphWidth(double width);

    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    void setContextMenuPressed(bool pressed);

    bool isPressed() const { return m_pressed; }
    void cancelPress();
    // In item coordinates.
    ui::PointF pressPosition() const { return m_pressPosition; }
    // In item coordinates; empty while hidden.
    ui::RectF plateRect() const;
    bool isPlateShown() const;
    ui::Color plateColor() const;

    // A second press gives no click when doubleClicked is set.
    std::function<void(const ui::PointerEvent &)> onClicked;
    std::function<void(const ui::PointerEvent &)> onPressed;
    std::function<void(const ui::PointerEvent &)> onDoubleClicked;
    std::function<void(const ui::PointerEvent &)> onPositionChanged;
    std::function<void()> onEntered;

    void barChanged() { requestPaint(); }

    ui::RectF centred(const ui::SizeF &size) const;

    bool acceptsPointer() const override { return true; }

protected:
    void pointerEnter() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerMove(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;
    void paint(ui::Painter &painter) override;

    BarContext *bar() const { return m_bar; }

private:
    double plateWidth(double scale) const;

    BarContext *m_bar;
    double m_leftInset = 0;
    double m_rightInset = 0;
    double m_glyphWidth = 0;
    bool m_checked = false;
    bool m_contextMenuPressed = false;
    bool m_pressed = false;
    bool m_doubleClicked = false;
    ui::MouseButton m_pressedButton = ui::MouseButton::Left;
    ui::PointF m_pressPosition;
};

class BarIcon : public ui::View
{
public:
    BarIcon();

    // Empty draws nothing.
    void setSource(std::string file, bool monochrome = false);
    void setGlyph(std::string name);
    void setColor(ui::Color color);
    void setDropShadow(bool shadow);

protected:
    void layout() override;

private:
    class Face;

    void update();

    std::string m_file;
    bool m_monochrome = false;
    std::string m_glyph;
    ui::Color m_color;
    bool m_dropShadow = false;
    ui::BlurredCopy *m_shadow = nullptr;
    Face *m_shadowFace = nullptr;
    Face *m_face = nullptr;
};

class BarLabel : public ui::View
{
public:
    BarLabel();

    void setText(std::string text);
    const std::string &text() const;
    void setWeight(int weight);
    void setFontFeatures(const std::vector<std::pair<std::string, int>> &features);
    void setDropShadow(bool shadow);
    void setTextColor(ui::Color color);

    ui::SizeF sizeHint() const override;

protected:
    void layout() override;

private:
    ui::BlurredCopy *m_shadow;
    ui::Label *m_shadowText;
    ui::StyledLabel *m_label;
    int m_weight = 400;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_BAR_ITEM_H_
