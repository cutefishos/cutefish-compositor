// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_VIEW_H_
#define CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_VIEW_H_

#include <functional>
#include <memory>

#include "ui/view.h"

namespace cutefish::ui {
class Animation;
} // namespace cutefish::ui

namespace cutefish::shell {

class CardItem;
class ControlCenterServices;
class DeviceUsersTile;
class MediaTile;
class StatusPanelView;
class TileSlider;

// The control centre's content without its window: the device-users tile,
// the cards, the media card and the two sliders, 360 wide.
class ControlCenterView : public ui::View
{
public:
    static constexpr double kWidth = 360;
    // Between the tiles and round them.
    static constexpr double kGap = 10;

    explicit ControlCenterView(ControlCenterServices &services);
    ~ControlCenterView() override;

    // Reads the services again.
    void refresh();
    // Takes its text again after the session language changed.
    void retranslate();
    // The window height at `scale`: the layout's, on whole device pixels.
    double windowHeight(double scale) const;

    CardItem *wirelessCard() const { return m_wireless; }
    CardItem *bluetoothCard() const { return m_bluetooth; }
    CardItem *darkModeCard() const { return m_darkMode; }
    CardItem *doNotDisturbCard() const { return m_doNotDisturb; }
    CardItem *cameraCard() const { return m_camera; }
    MediaTile *media() const { return m_media; }
    TileSlider *brightnessSlider() const { return m_brightness; }
    TileSlider *soundSlider() const { return m_sound; }

    // Press-and-hold on a card hides the control centre for Settings.
    std::function<void()> onHideRequested;
    // The Wi-Fi or Bluetooth card's chevron: its detail page.
    std::function<void()> onWirelessDetail;
    std::function<void()> onBluetoothDetail;
    // The Sound slider's chevron: the sound panel.
    std::function<void()> onSoundDetail;
    // The layout's height may have changed.
    std::function<void()> onHeightChanged;

protected:
    void layout() override;

private:
    double layoutHeight() const;

    ControlCenterServices &m_services;
    DeviceUsersTile *m_users;
    CardItem *m_wireless;
    CardItem *m_bluetooth;
    CardItem *m_darkMode;
    CardItem *m_doNotDisturb;
    CardItem *m_camera;
    CardItem *m_microphone;
    MediaTile *m_media;
    TileSlider *m_brightness;
    TileSlider *m_sound;
    bool m_darkModeShown = false;
};

// The control centre's pages: the tiles, and a tile's detail page (the
// Wi-Fi, Bluetooth or Sound panel) over them. Between the two the pages slide kSlide and
// cross-fade while the window's height glides from one to the other, on the
// panels' 380 ms curve; a change of direction on the way turns from where
// the pages are.
class ControlCenterPager : public ui::View
{
public:
    static constexpr double kSlide = 90;

    explicit ControlCenterPager(std::unique_ptr<ControlCenterView> main);
    ~ControlCenterPager() override;

    ControlCenterView *mainPage() const { return m_main; }
    // While shown, and while it slides away.
    StatusPanelView *detailPage() const { return m_detail; }
    bool isShowingDetail() const { return m_detail && m_target == 1; }
    bool isSliding() const;
    // 0 at the tiles, 1 at the detail page.
    double progress() const { return m_progress; }

    // `page` is shown at once at its height; its maximum height set.
    void showDetail(std::unique_ptr<StatusPanelView> page);
    void showMain();

    // The window's height at `scale`: the tiles' or the detail page's as it
    // is drawn, and between the two while the pages slide.
    double windowHeight(double scale) const;

    // windowHeight() may have changed.
    std::function<void()> onHeightChanged;

protected:
    void layout() override;

private:
    void slideTo(double target);
    void setProgress(double progress);
    void settle();
    void heightChanged();

    ControlCenterView *m_main;
    StatusPanelView *m_detail = nullptr;
    std::unique_ptr<ui::Animation> m_tween;
    double m_progress = 0;
    double m_from = 0;
    double m_target = 0;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_VIEW_H_
