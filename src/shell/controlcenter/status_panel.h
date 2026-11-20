// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_STATUS_PANEL_H_
#define CUTEFISH_SHELL_CONTROLCENTER_STATUS_PANEL_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/timer.h"
#include "shell/controlcenter/control_center_services.h"
#include "ui/view.h"

namespace cutefish::ui {
class Animation;
class BusyIndicator;
class Icon;
class Label;
class ScrollView;
class Switch;
} // namespace cutefish::ui

namespace cutefish::shell {

class LevelSlider;
class PanelRowList;
class SvgIcon;

namespace status_panel {
inline constexpr double kWidth = 320;
inline constexpr double kContentPadding = 12;
inline constexpr double kHorizontalPadding = 16;
inline constexpr double kRowInset = 6;
inline constexpr double kRowMargin = kHorizontalPadding - kRowInset;
inline constexpr double kBottomPadding = kContentPadding - kRowInset;
inline constexpr double kBackgroundMargin = kBottomPadding;
inline constexpr double kWindowRadius = 16;
inline constexpr double kBackgroundRadius = kWindowRadius - kBackgroundMargin;
inline constexpr double kHeaderHeight = 28;
inline constexpr double kSectionSpacing = 8;
inline constexpr double kSeparatorSpacing = 12;
inline constexpr std::chrono::milliseconds kHeightDuration {380};
} // namespace status_panel

class PanelRow : public ui::View
{
public:
    PanelRow();
    ~PanelRow() override;

    void setGlyph(std::string name);
    void setArtwork(std::string file);
    void setText(std::string text);
    void setDetail(std::string detail);
    void setActive(bool active);
    bool isActive() const { return m_active; }
    void setBold(bool bold);
    void setBusy(bool busy);
    void setLocked(bool locked);
    void setTrailingText(std::string text);
    void setRowEnabled(bool enabled);

    std::function<void()> onClicked;

    double heightForWidth(double width) const override;
    bool acceptsPointer() const override { return true; }

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;
    void hoverChanged() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;

private:
    double contentHeight() const;

    ui::Icon *m_glyph;
    SvgIcon *m_artwork;
    ui::Label *m_label;
    ui::Label *m_detail;
    ui::BusyIndicator *m_busy;
    ui::Icon *m_lock;
    ui::Label *m_trailing;
    bool m_active = false;
    bool m_pressed = false;
    bool m_rowEnabled = true;
};

class StatusPanelView : public ui::View
{
public:
    StatusPanelView(std::string title, std::string settingsText, std::string settingsPage,
                    ControlCenterServices &services);
    ~StatusPanelView() override;

    void setMaximumHeight(double height);
    // Whole logical pixels.
    double contentHeight() const;
    double shownBottom() const { return m_shownBottom; }
    void setShown(bool shown);
    bool isShown() const { return m_shown; }

    virtual void refresh() = 0;
    virtual void opened() {}
    virtual void retranslate() = 0;

    ui::View *settingsLink() const;
    // In the panel's coordinates.
    double footerLineY() const;

    std::function<void()> onShownBottomChanged;
    std::function<void()> onCloseRequested;

protected:
    void setTitles(std::string title, std::string settingsText);
    void setTitleDetail(std::string detail);
    // A child of the panel itself, outside the body's clip.
    void setAccessory(ui::View *accessory);
    void setBodyVisible(bool visible);
    virtual double bodyHeight() const = 0;
    // Places the body's views below `top`, in body()'s coordinates, whose x is
    // the panel's.
    virtual void layoutBody(double top) = 0;
    virtual double bodyInkInset() const = 0;
    void contentChanged();
    double bodyMaximumHeight() const;

    double panelWidth() const;
    ControlCenterServices &services() const { return m_services; }
    ui::View *body() const { return m_body; }

    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    class SettingsLink;

    double headerHeight() const;
    double inkInset() const;
    double footerHeight() const;
    double layoutHeight() const;
    double footerTop() const;
    void animateTo(double target);
    void setShownBottom(double bottom);

    ControlCenterServices &m_services;
    ui::View *m_clip;
    ui::View *m_body;
    ui::Label *m_title;
    ui::Label *m_detail;
    ui::View *m_accessory = nullptr;
    SettingsLink *m_link;
    std::string m_settingsPage;
    double m_maximumHeight = 0;
    double m_shownBottom = 0;
    double m_tweenFrom = 0;
    double m_tweenTarget = 0;
    double m_reach = 0;
    bool m_bodyVisible = true;
    bool m_shown = false;
    std::unique_ptr<ui::Animation> m_tween;
};

class NetworkPanel : public StatusPanelView
{
public:
    explicit NetworkPanel(ControlCenterServices &services);
    ~NetworkPanel() override;

    void refresh() override;
    void opened() override;
    void retranslate() override;
    void setShownForScans(bool shown);

    ui::Switch *wirelessSwitch() const { return m_switch; }
    const std::vector<PanelRow *> &rows() const { return m_rows; }

protected:
    double bodyHeight() const override;
    void layoutBody(double top) override;
    double bodyInkInset() const override;

private:
    double listHeight() const;
    double listLead() const;

    ui::Switch *m_switch;
    ui::ScrollView *m_scroll;
    ui::View *m_separator;
    PanelRowList *m_list;
    PanelRow *m_hotspot;
    std::vector<PanelRow *> m_rows;
    std::vector<WirelessNetwork> m_networks;
    base::Timer m_scanTimer;
};

class BluetoothPanel : public StatusPanelView
{
public:
    explicit BluetoothPanel(ControlCenterServices &services);
    ~BluetoothPanel() override;

    void refresh() override;
    void retranslate() override;

    ui::Switch *bluetoothSwitch() const { return m_switch; }
    const std::vector<PanelRow *> &rows() const { return m_rows; }

protected:
    double bodyHeight() const override;
    void layoutBody(double top) override;
    double bodyInkInset() const override;

private:
    double listHeight() const;
    double listLead() const;
    bool shownEnabled() const;

    ui::Switch *m_switch;
    ui::View *m_separator;
    ui::ScrollView *m_scroll;
    PanelRowList *m_list;
    std::vector<PanelRow *> m_rows;
    std::vector<BluetoothDevice> m_devices;
    std::optional<bool> m_requested;
    base::Timer m_requestTimer;
};

class VolumePanel : public StatusPanelView
{
public:
    explicit VolumePanel(ControlCenterServices &services);
    ~VolumePanel() override;

    void refresh() override;
    void retranslate() override;

    const std::vector<PanelRow *> &rows() const { return m_rows; }

protected:
    double bodyHeight() const override;
    void layoutBody(double top) override;
    double bodyInkInset() const override;

private:
    ui::Label *m_noOutput;
    LevelSlider *m_slider;
    ui::Label *m_heading;
    ui::View *m_separator;
    std::vector<PanelRow *> m_rows;
    std::vector<AudioOutput> m_outputs;
};

class BatteryPanel : public StatusPanelView
{
public:
    explicit BatteryPanel(ControlCenterServices &services);
    ~BatteryPanel() override;

    void refresh() override;
    void retranslate() override;

    const std::vector<PanelRow *> &rows() const { return m_rows; }

protected:
    double bodyHeight() const override;
    void layoutBody(double top) override;
    double bodyInkInset() const override;

private:
    ui::Label *m_source;
    ui::Label *m_state;
    ui::View *m_separator;
    ui::Label *m_heading;
    // power-saver, balanced, performance.
    std::vector<PanelRow *> m_rows;
    std::vector<std::string> m_profiles;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_STATUS_PANEL_H_
