// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/controlcenter/control_center_view.h"

#include <cmath>
#include <vector>

#include "base/i18n.h"
#include "shell/controlcenter/control_center_services.h"
#include "shell/controlcenter/level_slider.h"
#include "shell/controlcenter/panel_style.h"
#include "shell/controlcenter/tiles.h"
#include "ui/theme.h"

using cutefish::i18n::tr;

namespace cutefish::shell {

ControlCenterView::ControlCenterView(ControlCenterServices &services)
    : m_services(services)
{
    m_users = addChild<DeviceUsersTile>();

    m_wireless = addChild<CardItem>();
    m_wireless->setGlyph("network-wireless-connected-100");
    m_wireless->setTitle(tr("Wi-Fi"));
    m_wireless->onClicked = [this] { m_services.enableWireless(!m_wireless->isChecked()); };
    m_wireless->onPressAndHold = [this] {
        if (onHideRequested)
            onHideRequested();
        m_services.openSettings({"-m", "wlan"});
    };

    m_bluetooth = addChild<CardItem>();
    m_bluetooth->setArtwork("bluetooth-symbolic");
    m_bluetooth->setTitle(tr("Bluetooth"));
    m_bluetooth->onClicked = [this] { m_services.setBluetoothEnabled(!m_services.bluetoothEnabled()); };
    m_bluetooth->onPressAndHold = [this] {
        if (onHideRequested)
            onHideRequested();
        m_services.openSettings({"-m", "bluetooth"});
    };

    m_darkMode = addChild<CardItem>();
    m_darkMode->setArtwork("dark-mode");
    m_darkMode->setTitle(tr("Dark Mode"));
    m_darkMode->onClicked = [this] { m_services.setDarkMode(!ui::Theme::current().darkMode); };

    m_doNotDisturb = addChild<CardItem>();
    m_doNotDisturb->setArtwork("do-not-disturb");
    m_doNotDisturb->setTitle(tr("Do Not Disturb"));
    m_doNotDisturb->onClicked = [this] { m_services.setDoNotDisturb(!m_services.doNotDisturb()); };

    m_camera = addChild<CardItem>();
    m_camera->setArtwork("camera");
    m_camera->setTitle(tr("Camera"));
    m_camera->onClicked = [this] { m_services.setCameraEnabled(!m_camera->isChecked()); };

    m_microphone = addChild<CardItem>();
    m_microphone->setArtwork("microphone");
    m_microphone->setTitle(tr("Microphone"));
    m_microphone->onClicked = [this] { m_services.setMicrophoneEnabled(!m_microphone->isChecked()); };

    m_media = addChild<MediaTile>();
    m_media->onPrevious = [this] { m_services.mediaPrevious(); };
    m_media->onPlay = [this] { m_services.mediaPlay(); };
    m_media->onPause = [this] { m_services.mediaPause(); };
    m_media->onNext = [this] { m_services.mediaNext(); };

    // TRANSLATORS: the title of the screen brightness slider.
    m_brightness = addChild<TileSlider>(tr("Display"));
    m_brightness->slider()->setRange(1, 100, 1);
    m_brightness->slider()->setIcon(panel::artwork("brightness", false));
    m_brightness->slider()->onMoved = [this](double value) {
        m_services.setBrightness(int(std::lround(value)));
    };

    m_sound = addChild<TileSlider>(tr("Sound"));
    m_sound->slider()->setRange(0, 1, 0.01);
    m_sound->slider()->onMoved = [this](double value) {
        if (!m_services.hasSink())
            return;
        m_services.setVolume(value);
        m_services.setMuted(value == 0);
    };

    refresh();
}

ControlCenterView::~ControlCenterView() = default;

void ControlCenterView::retranslate()
{
    m_wireless->setTitle(tr("Wi-Fi"));
    m_bluetooth->setTitle(tr("Bluetooth"));
    m_darkMode->setTitle(tr("Dark Mode"));
    m_doNotDisturb->setTitle(tr("Do Not Disturb"));
    m_camera->setTitle(tr("Camera"));
    m_microphone->setTitle(tr("Microphone"));
    // TRANSLATORS: the title of the screen brightness slider.
    m_brightness->setTitle(tr("Display"));
    m_sound->setTitle(tr("Sound"));
    m_users->retranslate();
    refresh();
}

void ControlCenterView::refresh()
{
    const double before = layoutHeight();

    m_users->setUsers(m_services.deviceUsers());
    m_users->setVisible(!m_users->users().empty());

    m_wireless->setVisible(m_services.wirelessHardwareEnabled());
    m_wireless->setChecked(m_services.wirelessEnabled());
    const std::string network = m_services.wirelessName();
    m_wireless->setStatus(!m_wireless->isChecked() ? tr("Off") : !network.empty() ? network : tr("On"));

    m_bluetooth->setVisible(m_services.bluetoothAvailable());
    m_bluetooth->setChecked(m_services.bluetoothEnabled());
    m_bluetooth->setStatus(m_bluetooth->isChecked() ? tr("On") : tr("Off"));

    m_darkMode->setChecked(ui::Theme::current().darkMode);
    m_darkMode->setStatus(m_darkMode->isChecked() ? tr("On") : tr("Off"));
    m_doNotDisturb->setChecked(m_services.doNotDisturb());
    m_doNotDisturb->setStatus(m_doNotDisturb->isChecked() ? tr("On") : tr("Off"));
    m_camera->setChecked(m_services.cameraEnabled());
    m_camera->setStatus(m_camera->isChecked() ? tr("On") : tr("Off"));
    m_microphone->setChecked(m_services.microphoneEnabled());
    m_microphone->setStatus(m_microphone->isChecked() ? tr("On") : tr("Off"));

    m_media->setState(m_services.media());

    m_brightness->setVisible(m_services.brightnessEnabled());
    m_brightness->slider()->setValue(m_services.brightness());

    const bool sink = m_services.hasSink();
    m_sound->setVisible(sink);
    m_sound->slider()->setValue(sink ? m_services.volume() : 0);
    m_sound->slider()->setIcon(panel::artwork(volumeIconName(sink, m_services.volume()), false));

    requestLayout();
    if (layoutHeight() != before && onHeightChanged)
        onHeightChanged();
}

double ControlCenterView::layoutHeight() const
{
    const double width = kWidth - 2 * kGap;
    std::vector<double> heights;
    if (m_users->isVisible())
        heights.push_back(m_users->heightForWidth(width));
    int cards = 0;
    for (const CardItem *card : {m_wireless, m_bluetooth, m_darkMode, m_doNotDisturb, m_camera, m_microphone})
        cards += card->isVisible();
    const int rows = (cards + 1) / 2;
    if (rows > 0)
        heights.push_back(rows * CardItem::kHeight + (rows - 1) * kGap);
    heights.push_back(MediaTile::kHeight);
    if (m_brightness->isVisible())
        heights.push_back(m_brightness->heightForWidth(width));
    if (m_sound->isVisible())
        heights.push_back(m_sound->heightForWidth(width));
    double height = 0;
    for (double value : heights)
        height += value;
    return height + (heights.size() - 1) * kGap;
}

double ControlCenterView::windowHeight(double scale) const
{
    // Whole device pixels: the tiles' edges stay on the pixel grid.
    double size = std::ceil(layoutHeight() + kGap * 2);
    for (int i = 0; i < 120 && std::abs(size * scale - std::round(size * scale)) > 0.001; ++i)
        ++size;
    return size;
}

void ControlCenterView::layout()
{
    // A theme change relays the view out: the dark mode card follows it.
    if (m_darkModeShown != ui::Theme::current().darkMode) {
        m_darkModeShown = ui::Theme::current().darkMode;
        m_darkMode->setChecked(m_darkModeShown);
        m_darkMode->setStatus(m_darkModeShown ? tr("On") : tr("Off"));
        m_media->setState(m_services.media());
    }

    const double width = kWidth - 2 * kGap;
    double y = kGap;

    if (m_users->isVisible()) {
        const double height = m_users->heightForWidth(width);
        m_users->setGeometry({kGap, y, width, height});
        y += height + kGap;
    }

    const double cellWidth = (width - kGap) / 2;
    int index = 0;
    for (CardItem *card : {m_wireless, m_bluetooth, m_darkMode, m_doNotDisturb, m_camera, m_microphone}) {
        if (!card->isVisible())
            continue;
        const int row = index / 2;
        const int column = index % 2;
        card->setGeometry({kGap + column * (cellWidth + kGap), y + row * (CardItem::kHeight + kGap), cellWidth,
                           CardItem::kHeight});
        ++index;
    }
    const int rows = (index + 1) / 2;
    if (rows > 0)
        y += rows * CardItem::kHeight + (rows - 1) * kGap + kGap;

    m_media->setGeometry({kGap, y, width, MediaTile::kHeight});
    y += MediaTile::kHeight + kGap;

    for (TileSlider *slider : {m_brightness, m_sound}) {
        if (!slider->isVisible())
            continue;
        const double height = slider->heightForWidth(width);
        slider->setGeometry({kGap, y, width, height});
        y += height + kGap;
    }
}

std::string volumeIconName(bool hasSink, double volume)
{
    const double level = hasSink ? volume * 100.0 : -1;
    if (level <= 0)
        return "audio-volume-muted-symbolic";
    if (level <= 25)
        return "audio-volume-low-symbolic";
    if (level <= 75)
        return "audio-volume-medium-symbolic";
    return "audio-volume-high-symbolic";
}

} // namespace cutefish::shell
