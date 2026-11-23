// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

#include <linux/input-event-codes.h>

#include <gtest/gtest.h>

#include "base/dbus.h"
#include "compositor/compositor.h"
#include "core/main_loop.h"
#include "compositor/surface.h"
#include "fake_renderer.h"
#include "input/input_manager.h"
#include "input/seat.h"
#include "loop_test_util.h"
#include "scene/node.h"
#include "scene/scene.h"
#include "shell/controlcenter/control_center.h"
#include "shell/controlcenter/control_center_view.h"
#include "shell/controlcenter/level_slider.h"
#include "shell/controlcenter/power_dialog.h"
#include "shell/controlcenter/status_panel.h"
#include "shell/controlcenter/tiles.h"
#include "shell/osd/level_track.h"
#include "shell/shell.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "shell/menubar/menu_bar.h"
#include "shell/menubar/menu_bar_window.h"
#include "shell/menubar/status_icon.h"
#include "shell_test_util.h"
#include "temporary_directory.h"
#include "ui/application.h"
#include "ui/dialog_window.h"
#include "ui/icon.h"
#include "ui/label.h"
#include "ui/switch.h"
#include "ui/window.h"
#include "wm/window.h"
#include "wm/window_manager.h"

using namespace cutefish;
using Panel = shell::MenuBarPanels::Panel;

namespace {

input::Timestamp at(int ms)
{
    return input::Timestamp(std::chrono::milliseconds(ms));
}

const base::Rect kOutput(0, 0, 1440, 960);
constexpr int kMenuBar = 32;
constexpr int kDock = 92;

class FakeServices final : public shell::ControlCenterServices
{
public:
    bool wirelessHardwareEnabled() const override { return hardware; }
    bool wirelessEnabled() const override { return wireless; }
    bool hotspotActive() const override { return hotspot; }
    std::string hotspotName() const override { return "Laptop"; }
    std::string wirelessName() const override { return wireless ? "Office 5G" : ""; }
    std::string wirelessIcon() const override { return "network-wireless-connected-100"; }
    std::vector<shell::WirelessNetwork> wirelessNetworks() const override { return wireless ? networks : decltype(networks)(); }
    void enableWireless(bool enable) override { wireless = enable; changed(); }
    void requestScan() override { ++scans; }
    void stopHotspot() override { hotspot = false; changed(); }
    bool activateNetwork(const shell::WirelessNetwork &network) override
    {
        activated.push_back(network.name);
        return network.security == shell::WirelessNetwork::Security::Password && network.connectionPath.empty();
    }
    bool bluetoothAvailable() const override { return true; }
    bool bluetoothEnabled() const override { return bluetooth; }
    void setBluetoothEnabled(bool enabled) override
    {
        // BlueZ reports other changes before the adapter's power.
        if (!slowBluetooth)
            bluetooth = enabled;
        changed();
    }
    std::vector<shell::BluetoothDevice> bluetoothDevices() const override { return devices; }
    void setBluetoothDeviceConnected(const std::string &path, bool connected) override
    {
        connections.emplace_back(path, connected);
    }
    void setDarkMode(bool dark) override { darkRequested = dark ? 1 : 0; }
    bool doNotDisturb() const override { return dnd; }
    void setDoNotDisturb(bool enabled) override { dnd = enabled; changed(); }
    bool cameraEnabled() const override { return camera; }
    void setCameraEnabled(bool enabled) override { camera = enabled; changed(); }
    bool microphoneEnabled() const override { return microphone; }
    void setMicrophoneEnabled(bool enabled) override { microphone = enabled; changed(); }
    std::vector<shell::DeviceUser> deviceUsers() const override { return users; }
    shell::MediaState media() const override { return mediaState; }
    void mediaPrevious() override { media_.push_back("previous"); }
    void mediaPlay() override { media_.push_back("play"); }
    void mediaPause() override { media_.push_back("pause"); }
    void mediaNext() override { media_.push_back("next"); }
    bool brightnessEnabled() const override { return true; }
    int brightness() const override { return brightnessValue; }
    void setBrightness(int value) override { brightnessValue = value; ++brightnessWrites; }
    bool hasSink() const override { return sink; }
    double volume() const override { return volumeValue; }
    bool muted() const override { return mutedValue; }
    void setVolume(double volume) override { volumeValue = volume; }
    void setMuted(bool muted) override { mutedValue = muted; }
    std::vector<shell::AudioOutput> outputs() const override { return outputList; }
    void setDefaultOutput(uint32_t id) override { defaultRequested = int(id); }
    int chargePercent() const override { return 80; }
    bool onBattery() const override { return false; }
    std::string batteryStatus() const override { return "Charging paused at 80%"; }
    std::vector<std::string> powerProfiles() const override { return profiles; }
    std::string powerProfile() const override { return profile; }
    void setPowerProfile(const std::string &value) override { profile = value; changed(); }
    void openSettings(const std::vector<std::string> &arguments) override { settings.push_back(arguments); }

    bool hardware = true;
    bool wireless = true;
    bool hotspot = false;
    bool bluetooth = true;
    bool slowBluetooth = false;
    bool dnd = false;
    bool camera = false;
    bool microphone = false;
    int darkRequested = -1;
    int scans = 0;
    int brightnessValue = 63;
    int brightnessWrites = 0;
    bool sink = true;
    double volumeValue = 0.36;
    bool mutedValue = false;
    int defaultRequested = -1;
    std::string profile = "balanced";
    std::vector<std::string> profiles {"power-saver", "balanced"};
    std::vector<shell::WirelessNetwork> networks;
    std::vector<std::string> activated;
    std::vector<shell::BluetoothDevice> devices;
    std::vector<std::pair<std::string, bool>> connections;
    std::vector<shell::DeviceUser> users;
    shell::MediaState mediaState;
    std::vector<std::string> media_;
    std::vector<shell::AudioOutput> outputList {{1, "Speakers", true}, {2, "HDMI", false}};
    std::vector<std::vector<std::string>> settings;
};

shell::WirelessNetwork network(const std::string &name, shell::WirelessNetwork::State state,
                               shell::WirelessNetwork::Security security, const std::string &connection = {})
{
    shell::WirelessNetwork result;
    result.key = name;
    result.name = name;
    result.icon = "network-wireless-connected-66";
    result.state = state;
    result.security = security;
    result.connectionPath = connection;
    result.devicePath = "/dev/1";
    result.specificPath = "/ap/" + name;
    return result;
}

class ControlCenterTest : public testing::Test
{
protected:
    scene::Scene scene;
    scene::Node *surfaceContainer = scene.root()->addChild(std::make_unique<scene::Node>("surfaces"));
    compositor::Compositor compositor {surfaceContainer};
    testsupport::FakeRenderer renderer;
    input::Seat seat {&compositor};
    input::InputManager input {&seat};
    wm::WindowManager manager {&compositor, &seat};
    test::TemporaryDirectory directory;
    std::unique_ptr<shell::Shell> shell;
    std::unique_ptr<shell::ShellWindow> bar;
    FakeServices *services = nullptr;
    shell::ControlCenter *center = nullptr;
    int dock = 0;
    int time = 1000;

    void SetUp() override
    {
        shell::setShellDataDirectoryForTesting(CUTEFISH_SOURCE_DIR "/data/shell");
        scene.setSize(kOutput.size());
        manager.setOutputBounds(kOutput);
        manager.setExclusiveZone(&dock, wm::ScreenEdge::Bottom, kDock);
        input.setBounds(kOutput.size());
        input.setEventSink(&manager);
        shell::ShellEnvironment environment {&compositor, &manager, &seat};
        environment.servicesSettings = directory.filePath("services.conf");
        shell = std::make_unique<shell::Shell>(environment);
        shell->setApplications(std::make_unique<test::FakeApplications>());
        shell->setTaskWindows(std::make_unique<test::FakeTaskWindows>());

        bar = std::make_unique<shell::ShellWindow>(shell.get(), wm::WindowRole::Panel);
        bar->setContentView(std::make_unique<ui::View>());
        bar->setFocusPolicy(wm::FocusPolicy::NoFocus);
        bar->setGeometry(base::Rect(0, 0, kOutput.width(), kMenuBar));
        bar->setExclusiveZone(wm::ScreenEdge::Top, kMenuBar);
        bar->show();

        auto fake = std::make_unique<FakeServices>();
        services = fake.get();
        center = shell->addComponent(std::make_unique<shell::ControlCenter>(shell.get(), std::move(fake), nullptr));
        frame();
    }

    void TearDown() override
    {
        // The bar outlives the panels hanging from it, the shell the bar.
        center->closePanels();
        test::runFor(std::chrono::milliseconds(5));
        center = nullptr;
        bar.reset();
        shell.reset();
        seat.shutdown();
    }

    void frame() { compositor.updateTextures(renderer); }

    // One frame every 16 ms for `duration`.
    void frames(std::chrono::milliseconds duration)
    {
        for (std::chrono::milliseconds run {0}; run < duration; run += std::chrono::milliseconds(16)) {
            test::runFor(std::chrono::milliseconds(16));
            frame();
        }
    }

    void move(double x, double y) { input.onPointerMotionAbsolute(at(time += 5), {x, y}); }

    void press(double x, double y)
    {
        move(x, y);
        input.onPointerButton(at(time += 5), BTN_LEFT, true);
    }

    void release() { input.onPointerButton(at(time += 5), BTN_LEFT, false); }

    void click(double x, double y)
    {
        press(x, y);
        release();
        frame();
    }

    void key(uint32_t code)
    {
        input.onKey(at(time += 5), code, true);
        input.onKey(at(time += 5), code, false);
    }

    // x from the right edge.
    base::Rect item(int fromRight, int width = 40) const
    {
        return base::Rect(kOutput.width() - fromRight, 0, width, kMenuBar);
    }

    base::PointF centre(Panel panel, const ui::View *view) const
    {
        const ui::PointF origin = view->mapToWindow({0, 0});
        const base::Rect popup = center->popupWindow(panel)->drawnBounds();
        return {popup.x() + origin.x + view->geometry().width / 2, popup.y() + origin.y + view->geometry().height / 2};
    }

    void clickView(Panel panel, const ui::View *view)
    {
        const base::PointF point = centre(panel, view);
        click(point.x(), point.y());
    }

    void clickPlate(const shell::CardItem *card)
    {
        const ui::RectF plate = card->plateRect();
        const ui::PointF origin = card->mapToWindow({plate.x + plate.width / 2, plate.y + plate.height / 2});
        const base::Rect popup = center->popupWindow(Panel::ControlCenter)->drawnBounds();
        click(popup.x() + origin.x, popup.y() + origin.y);
    }

    // In the panel's coordinates.
    void expectFooterGaps(Panel panel, const shell::PanelRow *lastRow)
    {
        expectFooterGaps(center->statusPanel(panel), lastRow);
    }

    void expectFooterGaps(const shell::StatusPanelView *view, const shell::PanelRow *lastRow)
    {
        const double rowBottom = lastRow->mapToWindow({0, lastRow->geometry().height}).y;
        const double badgeBottom = rowBottom - (lastRow->geometry().height - 28) / 2;
        const double line = view->mapToWindow({0, view->footerLineY()}).y;
        const auto *label = static_cast<const ui::Label *>(view->settingsLink()->children().front().get());
        const double baseline = label->mapToWindow({0, 0}).y + label->baselineOffset();
        const double capTop = baseline - label->capHeight();
        // Within the line's snap to whole pixels and the label's.
        EXPECT_NEAR(14, line - badgeBottom, 0.5);
        EXPECT_NEAR(14, capTop - (line + 1), 1);
        EXPECT_NEAR(14, view->contentHeight() - baseline, 1);
    }
};

} // namespace

TEST_F(ControlCenterTest, TheControlCentreHangsBelowTheBarWithThePanelGlass)
{
    int changes = 0;
    base::Subscription subscription = center->panelsChanged.connect([&] { ++changes; });
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    ASSERT_TRUE(center->isOpen(Panel::ControlCenter));
    EXPECT_EQ(1, changes);
    shell::ShellWindow *window = center->popupWindow(Panel::ControlCenter);
    ASSERT_NE(nullptr, window);
    ASSERT_NE(nullptr, window->wmWindow());
    EXPECT_TRUE(window->wmWindow()->isPopup());
    // 6 from the edge, 6 below the bar.
    const base::Rect bounds = window->drawnBounds();
    EXPECT_EQ(base::Rect(1440 - 360 - 6, 38, 360, bounds.height()), bounds);
    EXPECT_GT(bounds.height(), 400);
    EXPECT_TRUE(window->surface()->glass().enabled);
    EXPECT_EQ(base::Rect(0, 0, 360, bounds.height()), window->surface()->backgroundBlurRect());
    EXPECT_EQ(16, window->surface()->backgroundBlurRadius());

    key(KEY_ESC);
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::ControlCenter));
    EXPECT_EQ(2, changes);

    center->openPanel(Panel::ControlCenter, bar.get(), base::Rect(100, 0, 40, kMenuBar));
    frame();
    EXPECT_EQ(103, center->popupWindow(Panel::ControlCenter)->drawnBounds().x());
    click(700, 600);
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::ControlCenter));
}

TEST_F(ControlCenterTest, CardsToggleTheirServices)
{
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    shell::ControlCenterView *view = center->controlCenterView();
    ASSERT_NE(nullptr, view);
    EXPECT_TRUE(view->wirelessCard()->isChecked());
    EXPECT_TRUE(view->bluetoothCard()->isChecked());
    EXPECT_FALSE(view->doNotDisturbCard()->isChecked());

    clickPlate(view->bluetoothCard());
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_FALSE(services->bluetooth);
    EXPECT_FALSE(view->bluetoothCard()->isChecked());
    EXPECT_FALSE(center->controlCenterPager()->isShowingDetail());
    clickView(Panel::ControlCenter, view->doNotDisturbCard());
    clickView(Panel::ControlCenter, view->cameraCard());
    clickView(Panel::ControlCenter, view->darkModeCard());
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_TRUE(services->dnd);
    EXPECT_TRUE(services->camera);
    EXPECT_EQ(1, services->darkRequested);
    EXPECT_TRUE(view->doNotDisturbCard()->isChecked());

    const int before = center->popupWindow(Panel::ControlCenter)->drawnBounds().height();
    services->hardware = false;
    services->bluetooth = false;
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    frame();
    EXPECT_FALSE(view->wirelessCard()->isVisible());
    EXPECT_EQ(before, center->popupWindow(Panel::ControlCenter)->drawnBounds().height());
}

TEST_F(ControlCenterTest, TheWifiBluetoothAndSoundTilesOpenTheirPanelsAsDetailPages)
{
    using State = shell::WirelessNetwork::State;
    using Security = shell::WirelessNetwork::Security;
    services->networks = {network("Office 5G", State::Activated, Security::Password, "/c/1"),
                          network("Cafe", State::Deactivated, Security::None)};
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    shell::ControlCenterView *view = center->controlCenterView();
    shell::ControlCenterPager *pager = center->controlCenterPager();
    ASSERT_NE(nullptr, pager);
    shell::CardItem *wireless = view->wirelessCard();
    EXPECT_TRUE(wireless->hasDetail());
    EXPECT_TRUE(view->bluetoothCard()->hasDetail());
    EXPECT_FALSE(view->darkModeCard()->hasDetail());
    shell::ShellWindow *window = center->popupWindow(Panel::ControlCenter);
    const int mainHeight = window->drawnBounds().height();

    EXPECT_EQ(0, wireless->chevron()->opacity());
    const base::PointF card = centre(Panel::ControlCenter, wireless);
    move(card.x(), card.y());
    frames(std::chrono::milliseconds(200));
    EXPECT_EQ(1, wireless->chevron()->opacity());
    const ui::RectF chevron = wireless->chevron()->geometry();
    EXPECT_DOUBLE_EQ(wireless->geometry().height / 2, chevron.y + chevron.height / 2);
    EXPECT_DOUBLE_EQ(12, wireless->geometry().width - chevron.x - chevron.width);

    const base::Rect popup = window->drawnBounds();
    click(card.x(), card.y());
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_TRUE(services->wireless);
    auto *panel = dynamic_cast<shell::NetworkPanel *>(pager->detailPage());
    ASSERT_NE(nullptr, panel);
    EXPECT_TRUE(pager->isShowingDetail());
    EXPECT_EQ(1, services->scans);
    frames(std::chrono::milliseconds(100));
    EXPECT_GT(pager->progress(), 0);
    EXPECT_LT(pager->progress(), 1);
    EXPECT_GT(panel->opacity(), 0);
    EXPECT_LT(view->opacity(), 1);
    EXPECT_LT(window->drawnBounds().width(), 360);
    EXPECT_GT(window->drawnBounds().width(), 320);
    EXPECT_EQ(1440 - 6, window->drawnBounds().right());
    EXPECT_GT(window->drawnBounds().height(), std::min(mainHeight, int(std::lround(panel->shownBottom()))));
    EXPECT_LT(window->drawnBounds().height(), std::max(mainHeight, int(std::lround(panel->shownBottom()))));
    frames(std::chrono::milliseconds(400));
    EXPECT_EQ(1, pager->progress());
    EXPECT_FALSE(view->isVisible());
    EXPECT_TRUE(panel->transform().isIdentity());
    const int detailHeight = int(std::lround(panel->shownBottom()));
    EXPECT_NE(mainHeight, detailHeight);
    EXPECT_EQ(base::Rect(1440 - 320 - 6, 38, 320, detailHeight), window->drawnBounds());
    EXPECT_EQ(base::Rect(0, 0, 320, detailHeight), window->surface()->backgroundBlurRect());
    ASSERT_EQ(2u, panel->rows().size());
    EXPECT_EQ(320 - 2 * 6, panel->rows()[0]->geometry().width);
    expectFooterGaps(panel, panel->rows().back());

    key(KEY_ESC);
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_TRUE(center->isOpen(Panel::ControlCenter));
    EXPECT_FALSE(pager->isShowingDetail());
    frames(std::chrono::milliseconds(400));
    EXPECT_EQ(nullptr, pager->detailPage());
    EXPECT_TRUE(view->isVisible());
    EXPECT_EQ(1, view->opacity());
    EXPECT_EQ(base::Rect(1440 - 360 - 6, 38, 360, mainHeight), window->drawnBounds());

    shell::TileSlider *sound = view->soundSlider();
    EXPECT_TRUE(sound->hasDetail());
    EXPECT_FALSE(view->brightnessSlider()->hasDetail());
    const base::PointF soundCentre = centre(Panel::ControlCenter, sound);
    move(soundCentre.x(), soundCentre.y());
    frames(std::chrono::milliseconds(200));
    EXPECT_EQ(1, sound->chevron()->opacity());
    const auto *soundTitle = static_cast<const ui::Label *>(sound->children().front().get());
    const ui::RectF soundChevron = sound->chevron()->geometry();
    const double capCentre = soundTitle->geometry().y + soundTitle->baselineOffset() - soundTitle->capHeight() / 2;
    EXPECT_NEAR(capCentre, soundChevron.y + soundChevron.height / 2, 0.5);
    EXPECT_DOUBLE_EQ(12, sound->geometry().width - soundChevron.x - soundChevron.width);
    EXPECT_LE(soundChevron.y + soundChevron.height, sound->slider()->geometry().y);
    const ui::PointF soundOrigin = sound->mapToWindow({soundTitle->geometry().x + 4, soundTitle->geometry().y + 4});
    click(popup.x() + soundOrigin.x, popup.y() + soundOrigin.y);
    frames(std::chrono::milliseconds(400));
    auto *volume = dynamic_cast<shell::VolumePanel *>(pager->detailPage());
    ASSERT_NE(nullptr, volume);
    ASSERT_EQ(2u, volume->rows().size());
    EXPECT_EQ(320 - 2 * 6, volume->rows()[0]->geometry().width);
    EXPECT_EQ(int(std::lround(volume->shownBottom())), window->drawnBounds().height());
    key(KEY_ESC);
    frames(std::chrono::milliseconds(420));
    EXPECT_EQ(nullptr, pager->detailPage());
    EXPECT_TRUE(center->isOpen(Panel::ControlCenter));

    services->devices = {{"/dev/a", "AirPods", "audio-headphones", true, false, 72}};
    shell::CardItem *bluetooth = view->bluetoothCard();
    clickView(Panel::ControlCenter, bluetooth);
    frames(std::chrono::milliseconds(400));
    EXPECT_TRUE(services->bluetooth);
    auto *devices = dynamic_cast<shell::BluetoothPanel *>(pager->detailPage());
    ASSERT_NE(nullptr, devices);
    ASSERT_EQ(1u, devices->rows().size());
    clickView(Panel::ControlCenter, devices->settingsLink());
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::ControlCenter));
    EXPECT_EQ((std::vector<std::string> {"-m", "bluetooth"}), services->settings.back());
}

TEST_F(ControlCenterTest, SlidersMoveTheLevelsAndTheMediaCardControlsThePlayer)
{
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    shell::ControlCenterView *view = center->controlCenterView();
    shell::LevelSlider *sound = view->soundSlider()->slider();
    EXPECT_NEAR(0.36, sound->position(), 1e-9);

    const ui::PointF origin = sound->mapToWindow({0, 0});
    const base::Rect popup = center->popupWindow(Panel::ControlCenter)->drawnBounds();
    const double travel = sound->geometry().width - sound->geometry().height;
    click(popup.x() + origin.x + sound->geometry().height / 2 + travel * 0.5, popup.y() + origin.y + 11);
    EXPECT_NEAR(0.5, services->volumeValue, 0.011);
    EXPECT_FALSE(services->mutedValue);

    input.onPointerAxis(at(time += 5), base::PointF(0, -10), true);
    frame();
    EXPECT_NEAR(0.55, services->volumeValue, 0.011);

    shell::LevelSlider *display = view->brightnessSlider()->slider();
    const ui::PointF displayOrigin = display->mapToWindow({0, 0});
    click(popup.x() + displayOrigin.x + display->geometry().height / 2, popup.y() + displayOrigin.y + 11);
    EXPECT_EQ(1, services->brightnessWrites);
    EXPECT_EQ(1, services->brightnessValue);

    shell::MediaTile *media = view->media();
    EXPECT_FALSE(media->playButton()->isEnabled());
    EXPECT_DOUBLE_EQ(0.35, media->playButton()->opacity());
    services->mediaState.hasPlayer = true;
    services->mediaState.playing = true;
    services->mediaState.title = "Song";
    services->mediaState.canControl = true;
    services->mediaState.canPause = true;
    services->mediaState.canGoNext = true;
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    frame();
    EXPECT_TRUE(media->playButton()->isEnabled());
    EXPECT_FALSE(media->previousButton()->isEnabled());
    clickView(Panel::ControlCenter, media->playButton());
    clickView(Panel::ControlCenter, media->nextButton());
    clickView(Panel::ControlCenter, media->previousButton());
    EXPECT_EQ((std::vector<std::string> {"pause", "next"}), services->media_);
}

TEST_F(ControlCenterTest, TheWifiPanelListsNetworksAndJoinsThem)
{
    using State = shell::WirelessNetwork::State;
    using Security = shell::WirelessNetwork::Security;
    services->networks = {network("Office 5G", State::Activated, Security::Password, "/c/1"),
                          network("Cafe", State::Deactivated, Security::None),
                          network("Home", State::Deactivated, Security::Password),
                          network("Busy", State::Activating, Security::Password, "/c/2")};
    center->openPanel(Panel::Network, bar.get(), item(293));
    frame();
    shell::ShellWindow *window = center->popupWindow(Panel::Network);
    ASSERT_NE(nullptr, window);
    const base::Rect bounds = window->drawnBounds();
    auto *panel = static_cast<shell::NetworkPanel *>(center->statusPanel(Panel::Network));
    const int shown = int(std::lround(panel->shownBottom()));
    EXPECT_EQ(int(panel->contentHeight()), shown);
    EXPECT_EQ(base::Rect(1440 - 320 - 6, 38, 320, shown), bounds);
    EXPECT_EQ(base::Rect(1440 - 320 - 6, 38, 320, 960 - kMenuBar - kDock - 12),
              center->panelGeometry(Panel::Network, bar.get(), item(293)));
    EXPECT_EQ(base::Rect(0, 0, 320, shown), window->surface()->backgroundBlurRect());
    EXPECT_FALSE(window->surface()->inputRegion());
    EXPECT_EQ(1, services->scans);

    ASSERT_EQ(4u, panel->rows().size());
    EXPECT_TRUE(panel->rows()[0]->isActive());
    expectFooterGaps(Panel::Network, panel->rows().back());
    EXPECT_FALSE(panel->rows()[1]->isActive());
    clickView(Panel::Network, panel->rows()[1]);
    clickView(Panel::Network, panel->rows()[3]);
    EXPECT_EQ((std::vector<std::string> {"Cafe"}), services->activated);
    EXPECT_TRUE(center->isOpen(Panel::Network));
    clickView(Panel::Network, panel->rows()[2]);
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::Network));

    center->openPanel(Panel::Network, bar.get(), item(293));
    frame();
    panel = static_cast<shell::NetworkPanel *>(center->statusPanel(Panel::Network));
    const double open = panel->shownBottom();
    clickView(Panel::Network, panel->wirelessSwitch());
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_FALSE(services->wireless);
    test::runFor(std::chrono::milliseconds(100));
    frame();
    EXPECT_LT(panel->shownBottom(), open);
    EXPECT_GT(panel->shownBottom(), panel->contentHeight());
    window = center->popupWindow(Panel::Network);
    const int shrinking = int(std::lround(panel->shownBottom()));
    EXPECT_EQ(shrinking, window->geometry().height());
    EXPECT_EQ(base::Rect(0, 0, 320, shrinking), window->surface()->backgroundBlurRect());
    test::runFor(std::chrono::milliseconds(400));
    frame();
    EXPECT_DOUBLE_EQ(panel->contentHeight(), panel->shownBottom());
    EXPECT_EQ(int(panel->contentHeight()), window->geometry().height());

    clickView(Panel::Network, panel->settingsLink());
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::Network));
    EXPECT_EQ((std::vector<std::string> {"-m", "wlan"}), services->settings.back());
}

TEST_F(ControlCenterTest, TheBluetoothPanelListsPairedDevicesAndConnectsThem)
{
    services->devices = {{"/dev/a", "AirPods", "audio-headphones", true, false, 72},
                         {"/dev/b", "Mouse", "input-mouse", false, false, -1},
                         {"/dev/c", "Keyboard", "input-keyboard", false, true, 40}};
    center->openPanel(Panel::Bluetooth, bar.get(), item(333));
    frame();
    auto *panel = static_cast<shell::BluetoothPanel *>(center->statusPanel(Panel::Bluetooth));
    ASSERT_NE(nullptr, panel);
    EXPECT_TRUE(panel->bluetoothSwitch()->isChecked());
    ASSERT_EQ(3u, panel->rows().size());
    EXPECT_TRUE(panel->rows()[0]->isActive());
    EXPECT_FALSE(panel->rows()[1]->isActive());
    expectFooterGaps(Panel::Bluetooth, panel->rows().back());

    clickView(Panel::Bluetooth, panel->rows()[0]);
    clickView(Panel::Bluetooth, panel->rows()[1]);
    clickView(Panel::Bluetooth, panel->rows()[2]);
    EXPECT_EQ((std::vector<std::pair<std::string, bool>> {{"/dev/a", false}, {"/dev/b", true}}),
              services->connections);

    services->slowBluetooth = true;
    const double open = panel->contentHeight();
    clickView(Panel::Bluetooth, panel->bluetoothSwitch());
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_TRUE(services->bluetooth);
    EXPECT_FALSE(panel->bluetoothSwitch()->isChecked());
    EXPECT_LT(panel->contentHeight(), open);
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_FALSE(panel->bluetoothSwitch()->isChecked());
    services->bluetooth = false;
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_FALSE(panel->bluetoothSwitch()->isChecked());

    clickView(Panel::Bluetooth, panel->settingsLink());
    test::runFor(std::chrono::milliseconds(20));
    EXPECT_FALSE(center->isOpen(Panel::Bluetooth));
    EXPECT_EQ((std::vector<std::string> {"-m", "bluetooth"}), services->settings.back());
}

TEST_F(ControlCenterTest, TheSoundAndBatteryPanelsPickOutputsAndModes)
{
    center->openPanel(Panel::Volume, bar.get(), item(333));
    frame();
    auto *volume = static_cast<shell::VolumePanel *>(center->statusPanel(Panel::Volume));
    ASSERT_EQ(2u, volume->rows().size());
    EXPECT_TRUE(volume->rows()[0]->isActive());
    expectFooterGaps(Panel::Volume, volume->rows().back());
    clickView(Panel::Volume, volume->rows()[0]);
    EXPECT_EQ(-1, services->defaultRequested);
    clickView(Panel::Volume, volume->rows()[1]);
    EXPECT_EQ(2, services->defaultRequested);

    center->closePanels();
    test::runFor(std::chrono::milliseconds(5));
    center->openPanel(Panel::Battery, bar.get(), item(247));
    frame();
    EXPECT_FALSE(center->isOpen(Panel::Volume));
    auto *battery = static_cast<shell::BatteryPanel *>(center->statusPanel(Panel::Battery));
    ASSERT_EQ(3u, battery->rows().size());
    EXPECT_TRUE(battery->rows()[0]->isVisible());
    EXPECT_TRUE(battery->rows()[1]->isActive());
    EXPECT_FALSE(battery->rows()[2]->isVisible());
    expectFooterGaps(Panel::Battery, battery->rows()[1]);
    clickView(Panel::Battery, battery->rows()[0]);
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_EQ("power-saver", services->profile);
    EXPECT_TRUE(battery->rows()[0]->isActive());
}

TEST_F(ControlCenterTest, LanguageChangeKeepsTheOpenPanels)
{
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    shell::ControlCenterView *view = center->controlCenterView();
    ASSERT_NE(nullptr, view);
    const int height = center->popupWindow(Panel::ControlCenter)->drawnBounds().height();
    ui::Application::instance()->languageChanged.emit();
    test::runFor(std::chrono::milliseconds(5));
    frame();
    EXPECT_TRUE(center->isOpen(Panel::ControlCenter));
    EXPECT_EQ(view, center->controlCenterView());
    EXPECT_EQ(height, center->popupWindow(Panel::ControlCenter)->drawnBounds().height());
    EXPECT_TRUE(view->wirelessCard()->isChecked());
    center->closePanels();
    test::runFor(std::chrono::milliseconds(5));

    center->openPanel(Panel::Battery, bar.get(), item(247));
    frame();
    auto *battery = static_cast<shell::BatteryPanel *>(center->statusPanel(Panel::Battery));
    ASSERT_NE(nullptr, battery);
    ui::Application::instance()->languageChanged.emit();
    test::runFor(std::chrono::milliseconds(5));
    frame();
    EXPECT_TRUE(center->isOpen(Panel::Battery));
    EXPECT_EQ(battery, center->statusPanel(Panel::Battery));
    ASSERT_EQ(3u, battery->rows().size());
    EXPECT_TRUE(battery->rows()[1]->isActive());
    EXPECT_FALSE(battery->rows()[2]->isVisible());
}

TEST_F(ControlCenterTest, TheBarIndicatorsFollowTheServices)
{
    int changes = 0;
    base::Subscription subscription = center->MenuBarIndicators::changed.connect([&] { ++changes; });
    EXPECT_TRUE(center->audio().hasSink);
    EXPECT_NEAR(0.36, center->audio().volume, 1e-9);
    EXPECT_EQ("Office 5G", center->network().wirelessName);
    services->wireless = false;
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_EQ(1, changes);
    EXPECT_FALSE(center->network().wirelessEnabled);
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_EQ(1, changes);
}

TEST_F(ControlCenterTest, ThePowerDialogConfirmsWithReturnAndCancelsWithEscape)
{
    shell::PowerDialog dialog(shell.get());
    int accepted = 0;
    int rejected = 0;
    dialog.onAccepted = [&] { ++accepted; };
    dialog.onRejected = [&] { ++rejected; };
    dialog.open(shell::PowerDialog::Action::Logout);
    frame();
    ASSERT_TRUE(dialog.isOpen());
    wm::Window *window = dialog.window()->wmWindow();
    ASSERT_NE(nullptr, window);
    EXPECT_EQ(wm::WindowRole::Normal, window->role());
    EXPECT_TRUE(window->isAlwaysOnTop());
    EXPECT_EQ(int(ui::AlertView::kWidth), window->bounds().width());
    dialog.open(shell::PowerDialog::Action::Reboot);
    EXPECT_EQ(shell::PowerDialog::Action::Reboot, dialog.action());
    key(KEY_ENTER);
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_EQ(1, accepted);
    EXPECT_FALSE(dialog.isOpen());

    dialog.open(shell::PowerDialog::Action::Shutdown);
    frame();
    key(KEY_ESC);
    test::runFor(std::chrono::milliseconds(5));
    EXPECT_EQ(1, rejected);
    EXPECT_FALSE(dialog.isOpen());
}

TEST_F(ControlCenterTest, TheDeviceUsersTileShowsWhoUsesTheCamera)
{
    center->openPanel(Panel::ControlCenter, bar.get(), item(178));
    frame();
    shell::ControlCenterView *view = center->controlCenterView();
    const int before = center->popupWindow(Panel::ControlCenter)->drawnBounds().height();
    const ui::RectF cards = view->wirelessCard()->geometry();
    services->users = {{"Camera", "org.example.Camera", true, true, false}};
    services->changed();
    test::runFor(std::chrono::milliseconds(5));
    frame();
    // One row of 20 with 16 round it, and the gap below.
    const int after = center->popupWindow(Panel::ControlCenter)->drawnBounds().height();
    EXPECT_NEAR(before + 20 + 32 + 10, after, 1);
    EXPECT_NEAR(cards.y + 62, view->wirelessCard()->geometry().y, 0.01);
    EXPECT_EQ("Camera is using the camera and microphone", shell::deviceUsageText(services->users.front()));
}

TEST_F(ControlCenterTest, TheBarsItemsOpenEveryPanel)
{
    bar->hide();
    directory.write("shell.conf", "");
    auto backend = std::make_unique<shell::MenuBar>(base::dbus::Connection::session(core::mainLoop()),
                                                    base::dbus::Connection::system(core::mainLoop()), &shell->tasks(),
                                                    directory.filePath("shell.conf"));
    auto *menuBar = shell->addComponent(std::make_unique<shell::MenuBarWindow>(shell.get(), std::move(backend)));
    menuBar->addPanels(center);
    menuBar->setIndicators(center);
    menuBar->menuBar().setControlShown(shell::MenuBar::Control::Bluetooth, true);
    frame();
    for (const auto &[panel, icon] : {std::pair {Panel::Volume, menuBar->volumeIcon()},
                                      std::pair {Panel::Bluetooth, menuBar->bluetoothIcon()},
                                      std::pair {Panel::Network, menuBar->networkIcon()},
                                      std::pair {Panel::ControlCenter, menuBar->controlIcon()}}) {
        ASSERT_TRUE(icon->isVisible());
        const ui::PointF origin = icon->mapToWindow({0, 0});
        click(origin.x + icon->geometry().width / 2, 16);
        test::runFor(std::chrono::milliseconds(50));
        frame();
        EXPECT_TRUE(center->isOpen(panel)) << int(panel);
        ASSERT_NE(nullptr, center->popupWindow(panel)) << int(panel);
        EXPECT_NE(nullptr, center->popupWindow(panel)->wmWindow()) << int(panel);
        EXPECT_TRUE(icon->isChecked());
    }
    menuBar->removePanels(center);
    menuBar->setIndicators(nullptr);
}
