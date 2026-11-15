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

// The level track made draggable, the control
// centre's sliders and the volume panel's. A press jumps the knob's centre to
// the pointer and a drag follows it; a wheel notch moves five steps and
// glides. The service's value is shown again only once the pointer is up and
// the last move has had 300 ms to come back, so it never flashes back.
class LevelSlider : public ui::View
{
public:
    static constexpr double kHeight = 22;

    LevelSlider();

    void setRange(double from, double to, double stepSize);
    // The service's value.
    void setValue(double value);
    // What the knob shows: the value, or the last move until it settles.
    double visualValue() const { return m_visualValue; }
    double position() const;
    // Artwork for the track's cap: the light variant in either theme.
    void setIcon(std::string file);

    // A wheel or touchpad turn, also from the tile around the slider.
    void wheel(const ui::ScrollEvent &event);

    // The value the user moved the knob to.
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

// A titled tile holding a level slider; the whole
// tile takes the wheel. A tile with a detail page shows a chevron at the
// title row's end while hovered, and a click anywhere off the slider opens
// the page.
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

// FishUI.Dpi's snap(): onto the device pixel grid of `scale`.
double snapToDevice(double value, double scale);
// Where an AlignedLabel goes in a layout at `top`: its baseline at
// `textBaseline` below it, the label placed by its line box.
double alignedLabelTop(const ui::Label *label, double textBaseline, double scale);
// The height an AlignedLabel takes in a column: its line less what its
// (possibly negative) top margin takes away.
double alignedLabelHeight(const ui::Label *label, double textBaseline, double scale);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_LEVEL_SLIDER_H_
