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

class ControlCenterView : public ui::View
{
public:
    static constexpr double kWidth = 360;
    static constexpr double kGap = 10;

    explicit ControlCenterView(ControlCenterServices &services);
    ~ControlCenterView() override;

    void refresh();
    void retranslate();
    double windowHeight(double scale) const;

    CardItem *wirelessCard() const { return m_wireless; }
    CardItem *bluetoothCard() const { return m_bluetooth; }
    CardItem *darkModeCard() const { return m_darkMode; }
    CardItem *doNotDisturbCard() const { return m_doNotDisturb; }
    CardItem *cameraCard() const { return m_camera; }
    MediaTile *media() const { return m_media; }
    TileSlider *brightnessSlider() const { return m_brightness; }
    TileSlider *soundSlider() const { return m_sound; }

    std::function<void()> onWirelessDetail;
    std::function<void()> onBluetoothDetail;
    std::function<void()> onSoundDetail;
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

class ControlCenterPager : public ui::View
{
public:
    static constexpr double kSlide = 90;

    explicit ControlCenterPager(std::unique_ptr<ControlCenterView> main);
    ~ControlCenterPager() override;

    ControlCenterView *mainPage() const { return m_main; }
    // Null unless shown or sliding away.
    StatusPanelView *detailPage() const { return m_detail; }
    bool isShowingDetail() const { return m_detail && m_target == 1; }
    bool isSliding() const;
    // 0 at the tiles, 1 at the detail page.
    double progress() const { return m_progress; }

    void showDetail(std::unique_ptr<StatusPanelView> page);
    void showMain();

    double windowWidth() const;
    double windowHeight(double scale) const;

    std::function<void()> onSizeChanged;

protected:
    void layout() override;

private:
    void slideTo(double target);
    void setProgress(double progress);
    void settle();
    void sizeChanged();

    ControlCenterView *m_main;
    StatusPanelView *m_detail = nullptr;
    std::unique_ptr<ui::Animation> m_tween;
    double m_progress = 0;
    double m_from = 0;
    double m_target = 0;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_VIEW_H_
