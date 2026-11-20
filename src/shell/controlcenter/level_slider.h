// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_LEVEL_SLIDER_H_
#define CUTEFISH_SHELL_CONTROLCENTER_LEVEL_SLIDER_H_

#include <functional>
#include <string>

#include "base/timer.h"
#include "ui/view.h"

namespace cutefish::ui {
class Label;
}

namespace cutefish::shell {

class DetailChevron;
class LevelTrack;

class LevelSlider : public ui::View
{
public:
    static constexpr double kHeight = 22;

    LevelSlider();

    void setRange(double from, double to, double stepSize);
    void setValue(double value);
    double visualValue() const { return m_visualValue; }
    double position() const;
    void setIcon(std::string file);

    void wheel(const ui::ScrollEvent &event);

    std::function<void(double value)> onMoved;

    bool acceptsPointer() const override { return true; }

protected:
    void layout() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerMove(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;
    bool scroll(const ui::ScrollEvent &event) override;

private:
    void setFromFraction(double fraction);
    void dragTo(double x);
    void setVisualValue(double value);

    LevelTrack *m_track;
    double m_from = 0;
    double m_to = 1;
    double m_stepSize = 0;
    double m_value = 0;
    double m_visualValue = 0;
    bool m_pressed = false;
    bool m_settling = false;
    bool m_gliding = false;
    base::Timer m_settle;
};

class TileSlider : public ui::View
{
public:
    explicit TileSlider(std::string title);

    LevelSlider *slider() const { return m_slider; }
    void setTitle(std::string title);
    void setHasDetail(bool detail);
    bool hasDetail() const { return m_hasDetail; }
    DetailChevron *chevron() const { return m_chevron; }
    double heightForWidth(double width) const override;
    ui::SizeF sizeHint() const override;

    std::function<void()> onDetail;

    bool acceptsPointer() const override { return m_hasDetail; }

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;
    void hoverChanged() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;
    bool scroll(const ui::ScrollEvent &event) override;

private:
    ui::Label *m_title;
    DetailChevron *m_chevron;
    LevelSlider *m_slider;
    bool m_hasDetail = false;
    bool m_detailPressed = false;
};

double snapToDevice(double value, double scale);
double alignedLabelTop(const ui::Label *label, double textBaseline, double scale);
double alignedLabelHeight(const ui::Label *label, double textBaseline, double scale);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_LEVEL_SLIDER_H_
