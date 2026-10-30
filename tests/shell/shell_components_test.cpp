// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The OSD and the dock as the compositor draws them: their windows, places,
// glass, input and behaviour, headless on a fake renderer.

#include <chrono>
#include <cmath>

#include <linux/input-event-codes.h>

#include <gtest/gtest.h>

#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "dbus_test_client.h"
#include "fake_renderer.h"
#include "input/drag.h"
#include "input/input_manager.h"
#include "input/seat.h"
#include "loop_test_util.h"
#include "scene/node.h"
#include "scene/scene.h"
#include "scene/surface_node.h"
#include "shell/common/app_launcher.h"
#include "shell/common/count_badge.h"
#include "shell/common/task_manager.h"
#include "shell/dock/dock.h"
#include "shell/dock/dock_icon.h"
#include "shell/dock/dock_presentation.h"
#include "shell/dock/minimized_windows.h"
#include "shell/dock/dock_tooltip.h"
#include "shell/dock/dock_window.h"
#include "shell/osd/level_track.h"
#include "shell/osd/osd.h"
#include "shell/shell.h"
#include "shell/shell_data.h"
#include "shell/shell_host.h"
#include "shell/shell_window.h"
#include "shell_test_util.h"
#include "temporary_directory.h"
#include "ui/drag.h"
#include "ui/embedded_window.h"
#include "ui/menu.h"
#include "wm/window.h"
#include "wm/window_manager.h"

using namespace cutefish;

namespace {

input::Timestamp at(int ms)
{
    return input::Timestamp(std::chrono::milliseconds(ms));
}

const base::Rect kOutput(0, 0, 1440, 960);
constexpr int kStatusBar = 32;

class FakeLevels final : public shell::OsdLevels
{
public:
    bool hasSink() const override { return true; }
    double volume() const override { return m_volume; }
    bool muted() const override { return m_muted; }
    void setVolume(double volume) override { m_volume = volume; }
    void setMuted(bool muted) override { m_muted = muted; }
    bool hasMicrophone() const override { return true; }
    double microphoneVolume() const override { return 0.5; }
    bool microphoneMuted() const override { return m_microphoneMuted; }
    void setMicrophoneMuted(bool muted) override { m_microphoneMuted = muted; }
    bool brightnessEnabled() const override { return true; }
    int brightness() const override { return m_brightness; }
    void setBrightness(int value) override { m_brightness = value; }
    int keyboardMaximum() const override { return 3; }
    int keyboardBrightness() const override { return m_keyboard; }
    void setKeyboardBrightness(int value) override { m_keyboard = value; }

    double m_volume = 0.36;
    bool m_muted = false;
    bool m_microphoneMuted = false;
    int m_brightness = 63;
    int m_keyboard = 1;
};

constexpr uint32_t kIconColor = 0xff3366cc;

// Starts a drag of `data` in `mime` on a press, as the launcher or the file
// manager would.
class DragSourceView final : public ui::View
{
public:
    DragSourceView(std::string mime, std::string data)
        : m_mime(std::move(mime))
        , m_data(std::move(data))
    {
    }

    bool acceptsPointer() const override { return true; }

    std::vector<ui::DropAction> finished;

protected:
    void pointerPress(const ui::PointerEvent &) override
    {
        ui::DragSource source;
        source.mimeTypes = {m_mime};
        source.data = [data = m_data](const std::string &) { return data; };
        source.actions = ui::CopyAction | ui::MoveAction;
        source.icon = ui::Bitmap(base::Size(8, 8), ui::Bitmap::Format::Rgba8888Premultiplied);
        source.icon.fill(kIconColor);
        source.hotSpot = {4, 4};
        source.onFinished = [this](ui::DropAction action) { finished.push_back(action); };
        startDrag(std::move(source));
    }

private:
    std::string m_mime;
    std::string m_data;
};

class ShellComponentsTest : public testing::Test
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
    test::FakeApplications *applications = nullptr;
    test::FakeTaskWindows *windows = nullptr;
    std::unique_ptr<shell::Shell> shell;
    int statusBar = 0;

    void SetUp() override
    {
        shell::setShellDataDirectoryForTesting(CUTEFISH_SOURCE_DIR "/data/shell");
        scene.setSize(kOutput.size());
        manager.setOutputBounds(kOutput);
        manager.setExclusiveZone(&statusBar, wm::ScreenEdge::Top, kStatusBar);
        input.setBounds(kOutput.size());
        input.setEventSink(&manager);
        shell::ShellEnvironment environment {&compositor, &manager, &seat};
        environment.servicesSettings = directory.filePath("services.conf");
        shell = std::make_unique<shell::Shell>(environment);
        auto apps = std::make_unique<test::FakeApplications>();
        applications = apps.get();
        applications->addSimple("files", "Files", "folder");
        applications->addSimple("terminal", "Terminal", "utilities-terminal");
        applications->scan();
        shell->setApplications(std::move(apps));
        auto tasks = std::make_unique<test::FakeTaskWindows>();
        windows = tasks.get();
        shell->setTaskWindows(std::move(tasks));
    }

    void TearDown() override
    {
        shell.reset();
        seat.shutdown();
    }

    void frame() { compositor.updateTextures(renderer); }

    void move(int ms, double x, double y) { input.onPointerMotionAbsolute(at(ms), {x, y}); }

    void click(int ms, double x, double y, uint32_t button)
    {
        move(ms, x, y);
        input.onPointerButton(at(ms), button, true);
        input.onPointerButton(at(ms + 1), button, false);
    }

    // An icon's centre on the screen.
    static base::PointF centreOf(shell::DockWindow *dock, shell::DockIcon *icon)
    {
        const base::Rect window = dock->window()->geometry();
        const ui::RectF rect = icon->iconRect();
        return base::PointF(window.x() + icon->geometry().x + rect.x + rect.width / 2,
                            window.y() + icon->geometry().y + rect.y + rect.height / 2);
    }

    std::vector<std::string> dockIds(shell::DockWindow *dock)
    {
        std::vector<std::string> ids;
        const shell::DockModel &model = dock->dock().applications();
        for (int row = 0; row < model.count(); ++row)
            ids.push_back(model.at(row).id);
        return ids;
    }

    // Presses at `from` and moves past the drag distance to `to`, on the
    // loop turn the drag starts on.
    void beginDrag(int ms, const base::PointF &from, const base::PointF &to)
    {
        move(ms, from.x(), from.y());
        input.onPointerButton(at(ms + 1), BTN_LEFT, true);
        move(ms + 2, from.x() + (to.x() - from.x()) / 2, from.y() + (to.y() - from.y()) / 2);
        test::processPendingEvents();
        move(ms + 3, to.x(), to.y());
    }

    void release(int ms) { input.onPointerButton(at(ms), BTN_LEFT, false); }

    // A window of the shell's own the external drags start from.
    std::unique_ptr<shell::ShellWindow> sourceWindow(const std::string &mime, const std::string &data,
                                                     DragSourceView **view)
    {
        auto window = std::make_unique<shell::ShellWindow>(shell.get(), wm::WindowRole::Panel);
        auto content = std::make_unique<DragSourceView>(mime, data);
        *view = content.get();
        window->setContentView(std::move(content));
        window->setGeometry(base::Rect(0, 0, 200, kStatusBar));
        window->show();
        frame();
        return window;
    }

    shell::DockWindow *openDock(const std::string &settings)
    {
        directory.write("shell.conf", settings);
        auto dock = std::make_unique<shell::Dock>(nullptr, shell->tasks(), shell->launcher(),
                                                  directory.filePath("shell.conf"),
                                                  std::make_unique<shell::TrashMonitor>(
                                                          directory.filePath("mountinfo"),
                                                          directory.filePath("Trash")));
        shell::Dock *backend = dock.get();
        auto *view = shell->addComponent(std::make_unique<shell::DockWindow>(shell.get(), std::move(dock)));
        EXPECT_TRUE(test::runUntil([backend] {
            return backend->applications().isLoaded() && backend->settings().isReady();
        }));
        frame();
        return view;
    }
};

} // namespace

TEST_F(ShellComponentsTest, TheOsdShowsTopCentreBelowTheBarAndGoes)
{
    auto levels = std::make_unique<FakeLevels>();
    FakeLevels *values = levels.get();
    auto *osd = shell->addComponent(std::make_unique<shell::Osd>(shell.get(), std::move(levels)));
    EXPECT_FALSE(osd->isShown());

    // Ash's 4 % steps: 0.36 is step 9, up is 0.40.
    osd->volumeUp();
    ASSERT_TRUE(osd->isShown());
    wm::Window *window = osd->window()->wmWindow();
    ASSERT_NE(nullptr, window);
    EXPECT_EQ(wm::WindowRole::Osd, window->role());
    EXPECT_EQ(wm::FocusPolicy::NoFocus, window->focusPolicy());
    EXPECT_EQ(base::Rect(580, kStatusBar + 6, 280, 54), window->bounds());
    // Clicks go through.
    ASSERT_TRUE(osd->window()->surface()->inputRegion());
    EXPECT_TRUE(osd->window()->surface()->inputRegion()->isEmpty());
    EXPECT_NEAR(0.40, values->m_volume, 1e-9);
    EXPECT_NEAR(0.40, osd->track()->position(), 1e-9);
    // The first level after showing snaps.
    EXPECT_NEAR(0.40, osd->track()->drawnPosition(), 1e-9);
    EXPECT_EQ(shell::shellDataFile("audio-volume-medium-symbolic.svg"), osd->track()->icon());
    EXPECT_TRUE(osd->window()->surface()->glass().enabled);
    EXPECT_EQ(base::Rect(0, 0, 280, 54), osd->window()->surface()->backgroundBlurRect());
    EXPECT_EQ(18, osd->window()->surface()->backgroundBlurRadius());
    frame();

    // Muted shows at zero with the muted icon; down while muted only shows.
    osd->toggleMute();
    EXPECT_TRUE(values->m_muted);
    EXPECT_EQ(0, osd->track()->position());
    EXPECT_EQ(shell::shellDataFile("audio-volume-muted-symbolic.svg"), osd->track()->icon());
    osd->volumeDown();
    EXPECT_NEAR(0.40, values->m_volume, 1e-9);

    // Sixteen brightness steps: 63 is step 10, up is 69.
    osd->brightnessUp();
    EXPECT_EQ(shell::Osd::Kind::Brightness, osd->kind());
    EXPECT_EQ(69, values->m_brightness);
    // A backlight of three levels steps by one.
    osd->keyboardBrightnessUp();
    EXPECT_EQ(2, values->m_keyboard);
    values->keyboardChangedByFirmware();
    EXPECT_EQ(shell::Osd::Kind::KeyboardBrightness, osd->kind());

    // Gone 1.5 s after the last key.
    test::runFor(std::chrono::milliseconds(1700));
    EXPECT_FALSE(osd->isShown());
    EXPECT_EQ(nullptr, osd->window()->wmWindow());
}

TEST_F(ShellComponentsTest, TheOsdVolumeUpKeepsAVolumeAboveNormal)
{
    auto levels = std::make_unique<FakeLevels>();
    FakeLevels *values = levels.get();
    values->m_volume = 1.5;
    auto *osd = shell->addComponent(std::make_unique<shell::Osd>(shell.get(), std::move(levels)));

    osd->volumeUp();
    EXPECT_NEAR(1.5, values->m_volume, 1e-9);
    EXPECT_EQ(1, osd->track()->position());

    // Down still steps from where it is.
    osd->volumeDown();
    EXPECT_NEAR(1.48, values->m_volume, 1e-9);

    values->m_volume = 0.98;
    test::runFor(std::chrono::milliseconds(1700));
    osd->volumeUp();
    EXPECT_NEAR(1.0, values->m_volume, 1e-9);
}

TEST_F(ShellComponentsTest, TheOsdServesOrgCutefishOsd)
{
    auto levels = std::make_unique<FakeLevels>();
    FakeLevels *values = levels.get();
    shell::ShellEnvironment environment {&compositor, &manager, &seat,
                                         base::dbus::Connection::session(core::mainLoop())};
    // A shell of its own on the session bus.
    shell.reset();
    shell = std::make_unique<shell::Shell>(environment);
    auto *osd = shell->addComponent(std::make_unique<shell::Osd>(shell.get(), std::move(levels)));
    ASSERT_TRUE(osd->registerService());
    test::DBusTestClient client;
    client.call("org.cutefish.Osd", "/Osd", "org.cutefish.Osd", "brightnessDown");
    EXPECT_EQ(56, values->m_brightness);
    EXPECT_TRUE(osd->isShown());
    client.call("org.cutefish.Osd", "/Osd", "org.cutefish.Osd", "toggleMicrophoneMute");
    EXPECT_TRUE(values->m_microphoneMuted);
    EXPECT_EQ(shell::shellDataFile("microphone-muted.svg"), osd->track()->icon());
}

// As CompositorShell starts them: the components, then their names and objects
// on the session connection.
TEST_F(ShellComponentsTest, TheOsdAndTheDockServeTheirNames)
{
    base::dbus::Connection *bus = base::dbus::Connection::session(core::mainLoop());
    ASSERT_NE(nullptr, bus);

    auto levels = std::make_unique<FakeLevels>();
    FakeLevels *values = levels.get();
    shell::ShellEnvironment environment {&compositor, &manager, &seat, bus};
    environment.servicesSettings = directory.filePath("services.conf");
    shell.reset();
    shell = std::make_unique<shell::Shell>(environment);
    auto apps = std::make_unique<test::FakeApplications>();
    apps->addSimple("files", "Files", "folder");
    apps->scan();
    shell->setApplications(std::move(apps));
    shell->setTaskWindows(std::make_unique<test::FakeTaskWindows>());

    auto *osd = shell->addComponent(std::make_unique<shell::Osd>(shell.get(), std::move(levels)));
    directory.write("shell.conf", "[Dock]\nPinned=files\n");
    auto dock = std::make_unique<shell::Dock>(bus, shell->tasks(), shell->launcher(), directory.filePath("shell.conf"),
                                              std::make_unique<shell::TrashMonitor>(
                                                      directory.filePath("mountinfo"), directory.filePath("Trash")));
    shell::Dock *dockService = dock.get();
    shell->addComponent(std::make_unique<shell::DockWindow>(shell.get(), std::move(dock)));
    ASSERT_TRUE(test::runUntil([dockService] {
        return dockService->applications().isLoaded() && dockService->settings().isReady();
    }));
    ASSERT_TRUE(osd->registerService());
    ASSERT_TRUE(dockService->registerService());

    test::DBusTestClient client;
    const std::string osdXml = client.introspect("org.cutefish.Osd", "/Osd");
    EXPECT_NE(std::string::npos, osdXml.find("<interface name=\"org.cutefish.Osd\">")) << osdXml;
    EXPECT_NE(std::string::npos, osdXml.find("<method name=\"brightnessUp\">")) << osdXml;
    const std::string dockXml = client.introspect("org.cutefish.Dock", "/Dock");
    EXPECT_NE(std::string::npos, dockXml.find("<interface name=\"org.cutefish.Dock\">")) << dockXml;
    EXPECT_NE(std::string::npos, dockXml.find("<method name=\"setMinimizedArea\">")) << dockXml;

    client.call("org.cutefish.Osd", "/Osd", "org.cutefish.Osd", "brightnessUp");
    EXPECT_EQ(69, values->m_brightness);
    EXPECT_TRUE(osd->isShown());
    client.call("org.cutefish.Dock", "/Dock", "org.cutefish.Dock", "setMinimizedArea", false);
    EXPECT_TRUE(test::runUntil([dockService] { return !dockService->settings().minimizedArea(); }));
}

TEST_F(ShellComponentsTest, TheDockSitsAlongTheBottomOnItsGlassAndReservesItsStrip)
{
    auto *dock = openDock("[Dock]\nPinned=files, terminal\n");
    wm::Window *window = dock->window()->wmWindow();
    ASSERT_NE(nullptr, window);
    EXPECT_EQ(wm::WindowRole::Dock, window->role());
    EXPECT_EQ(wm::FocusPolicy::NoFocus, window->focusPolicy());
    // The strip along the whole edge, 86 thick at the default icon size,
    // reaching over its 5 px gap to the edge and 11 past the far side for
    // the 27 a launching icon bounces.
    EXPECT_EQ(base::Rect(5, 960 - 86 - 5 - 11, 1430, 102), window->bounds());
    // From the screen edge to the far side, and 2 more.
    EXPECT_EQ(base::Rect(0, kStatusBar, 1440, 960 - kStatusBar - 92), manager.workAreaFor(nullptr));

    // Launcher and two pins a pitch of 70 each, the divider's 24, then the
    // trash and the two end paddings.
    const int length = 3 * 70 + 24 + 54 + 2 * 16;
    const base::Rect panel = dock->panelRect();
    EXPECT_EQ(length, panel.width());
    EXPECT_EQ(86, panel.height());
    EXPECT_EQ(int(std::floor((1430 - length) / 2.0)), panel.x());
    EXPECT_EQ(11, panel.y());
    EXPECT_EQ(panel.translated(5, 960 - 86 - 5 - 11), dock->primaryGeometry());

    // The glass: the default tint's values, rounded to 16 bits, then wl_fixed.
    const compositor::Surface *surface = dock->window()->surface();
    EXPECT_EQ(panel, surface->backgroundBlurRect());
    EXPECT_EQ(28, surface->backgroundBlurRadius());
    const render::GlassParams glass = surface->glass();
    EXPECT_TRUE(glass.enabled);
    EXPECT_FLOAT_EQ(1.0f, glass.blurSigma);
    EXPECT_FLOAT_EQ(1.0f, glass.rim);
    EXPECT_FLOAT_EQ(0.0f, glass.chroma);
    EXPECT_FLOAT_EQ(shell::glassFixed(1.3), glass.saturation);
    EXPECT_NEAR(0.6, glass.tintRed, 1.0 / 256);
    EXPECT_NEAR(0.25, glass.tintAlpha, 1.0 / 256);
    // Input only on the rounded panel.
    ASSERT_TRUE(surface->inputRegion());
    EXPECT_TRUE(surface->inputRegion()->contains(base::Point(panel.x() + panel.width() / 2, panel.y() + 2)));
    EXPECT_FALSE(surface->inputRegion()->contains(base::Point(panel.x(), panel.y())));
    EXPECT_FALSE(surface->inputRegion()->contains(base::Point(2, 40)));
    EXPECT_FALSE(surface->inputRegion()->contains(base::Point(panel.x() + panel.width() / 2, panel.y() - 1)));
    // And on the gap under it, where the icon above still has the pointer.
    EXPECT_TRUE(surface->inputRegion()->contains(base::Point(panel.x(), 100)));
    EXPECT_FALSE(surface->inputRegion()->contains(base::Point(panel.x() - 1, 100)));
    EXPECT_EQ(91.0, dock->icons()[1]->geometry().height);

    // Icons in order along the panel, each slot a pitch of 70 on.
    const auto icons = dock->icons();
    ASSERT_EQ(4u, icons.size());
    for (size_t i = 1; i + 1 < icons.size(); ++i)
        EXPECT_EQ(70, icons[i]->geometry().x - icons[i - 1]->geometry().x);
    EXPECT_EQ(shell::DockIcon::Kind::Trash, icons.back()->kind());
    // The divider after the pins: its line centred in the gap, 48 long, 19 down.
    EXPECT_EQ(19, dock->divider().y());
    EXPECT_EQ(48, dock->divider().height());
}

TEST_F(ShellComponentsTest, ALaunchingDockIconBouncesOnceIntoItsRoomWithItsTooltip)
{
    auto *dock = openDock("[Dock]\nPinned=files\n");
    shell::DockIcon *icon = dock->applicationIcon("files");
    ASSERT_NE(nullptr, icon);
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return !dock->applicationPresentation()->isRunning();
    }));
    const ui::View *face = nullptr;
    for (const auto &child : icon->children()) {
        if (child->isVisible())
            face = child.get();
    }
    ASSERT_NE(nullptr, face);
    const base::PointF centre = centreOf(dock, icon);
    move(1, centre.x(), centre.y());
    ASSERT_TRUE(dock->tooltip()->isShown());
    shell::ShellWindow *popup =
            shell->host()->windowFor(static_cast<ui::EmbeddedWindow *>(dock->tooltip()->window()));
    ASSERT_NE(nullptr, popup);
    const base::Rect tip = popup->geometry();

    // Straight up from the screen edge, as high as the room past the panel
    // allows, and down to rest; the tooltip rides along.
    const auto started = std::chrono::steady_clock::now();
    icon->bounce();
    double highest = 0;
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        const ui::PointF offset = face->transform().translation;
        EXPECT_EQ(0, offset.x);
        EXPECT_EQ(std::round(offset.y), offset.y);
        EXPECT_EQ(tip.translated(0, int(offset.y)), popup->geometry());
        highest = std::min(highest, offset.y);
        return !icon->isBouncing();
    }));
    EXPECT_GE(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(620));
    EXPECT_NEAR(-27, highest, 1);
    EXPECT_EQ(ui::PointF(), face->transform().translation);
    EXPECT_EQ(tip, popup->geometry());
    EXPECT_GE(icon->geometry().y + icon->iconRect().y + highest, 0);
}

TEST_F(ShellComponentsTest, TheDockFollowsRunningApplicationsAndTakesClicks)
{
    auto *dock = openDock("[Dock]\nPinned=files\n");
    shell::TaskWindow terminal;
    terminal.appId = "terminal";
    terminal.title = "Terminal";
    terminal.pid = 4242;
    const uint64_t id = windows->add(terminal);
    ASSERT_TRUE(test::runUntil([dock] { return dock->applicationIcon("terminal"); }));
    shell::DockIcon *icon = dock->applicationIcon("terminal");
    EXPECT_TRUE(icon->isRunning());
    EXPECT_FALSE(dock->applicationIcon("files")->isRunning());
    // It grows out of its centre.
    bool grew = false;
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        const ui::RectF rect = icon->iconRect();
        const ui::PointF centre = icon->mapToWindow({rect.x + rect.width / 2, rect.y + rect.height / 2});
        for (const auto &child : icon->children()) {
            if (child->transform().scale == 1 || child->transform().scale == 0)
                continue;
            grew = true;
            EXPECT_FALSE(icon->isDotShown());
            const ui::PointF start = child->mapToWindow({0, 0});
            const ui::PointF end = child->mapToWindow({child->geometry().width, child->geometry().height});
            EXPECT_NEAR(centre.x, (start.x + end.x) / 2, 1e-6);
            EXPECT_NEAR(centre.y, (start.y + end.y) / 2, 1e-6);
        }
        return !dock->applicationPresentation()->isRunning();
    }));
    EXPECT_TRUE(grew);
    EXPECT_TRUE(icon->isDotShown());
    // A running application past the pins brings the trailing divider.
    test::runFor(std::chrono::milliseconds(400));
    frame();
    EXPECT_FALSE(dock->trailingDivider().isEmpty());

    const base::Rect window = dock->window()->geometry();
    const auto centre = [&](shell::DockIcon *slot) {
        const ui::RectF rect = slot->iconRect();
        return base::PointF(window.x() + slot->geometry().x + rect.x + rect.width / 2,
                            window.y() + slot->geometry().y + rect.y + rect.height / 2);
    };
    // Hovering shows its name; the next icon takes the tooltip over.
    const base::PointF terminalCentre = centre(icon);
    move(1, terminalCentre.x(), terminalCentre.y());
    ASSERT_TRUE(dock->tooltip()->isShown());
    EXPECT_EQ("Terminal", dock->tooltip()->text());
    const auto tip = [&] {
        return shell->host()->windowFor(static_cast<ui::EmbeddedWindow *>(dock->tooltip()->window()));
    };
    shell::ShellWindow *popup = tip();
    ASSERT_NE(nullptr, popup);
    const base::Rect terminalTip = popup->geometry();
    const base::PointF filesCentre = centre(dock->applicationIcon("files"));
    move(2, filesCentre.x(), filesCentre.y());
    EXPECT_EQ("Files", dock->tooltip()->text());
    // The popup is the bubble, centred over its icon and just above the dock,
    // and the next icon moves it rather than making another.
    EXPECT_EQ(popup, tip());
    const base::Rect filesTip = popup->geometry();
    EXPECT_NE(terminalTip.x(), filesTip.x());
    EXPECT_NEAR(filesCentre.x(), filesTip.x() + filesTip.width() / 2.0, 1);
    EXPECT_NEAR(terminalCentre.x(), terminalTip.x() + terminalTip.width() / 2.0, 1);
    EXPECT_LT(filesTip.width(), 120);
    EXPECT_LT(filesTip.y() + filesTip.height(), window.y() + dock->panelRect().y());

    // A click on a running application shows its window.
    click(3, terminalCentre.x(), terminalCentre.y(), BTN_LEFT);
    ASSERT_FALSE(windows->requests.empty());
    EXPECT_EQ("activate " + std::to_string(id), windows->requests.back());

    // The right button opens its menu: the window, then Pin and Quit; the
    // tooltip goes and the icon stays dimmed while it is open.
    click(4, terminalCentre.x(), terminalCentre.y(), BTN_RIGHT);
    ui::Menu *menu = dock->openMenu();
    ASSERT_NE(nullptr, menu);
    EXPECT_FALSE(dock->tooltip()->isShown());
    EXPECT_TRUE(icon->isDimmed());
    menu->close();
    EXPECT_FALSE(icon->isDimmed());

    // Gone, it fades out above the dock where it was while the dock closes
    // up, then leaves.
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return !dock->applicationPresentation()->isRunning();
    }));
    const ui::RectF leavingAt = icon->geometry();
    const double leavingScale = icon->presentationScale();
    windows->remove(id);
    bool faded = false;
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        if (shell::DockIcon *leaving = dock->applicationIcon("terminal")) {
            EXPECT_EQ(leavingAt, leaving->geometry());
            EXPECT_EQ(leavingScale, leaving->presentationScale());
            EXPECT_EQ(leaving, leaving->parent()->children().back().get());
            EXPECT_EQ(window, dock->window()->geometry());
            faded = faded || (leaving->presentationOpacity() > 0 && leaving->presentationOpacity() < 1);
        }
        return !dock->applicationIcon("terminal");
    }));
    EXPECT_TRUE(faded);
}

TEST_F(ShellComponentsTest, AnApplicationsCountSitsOnItsIconsCorner)
{
    namespace dbus = base::dbus;
    dbus::Connection *bus = dbus::Connection::session(core::mainLoop());
    ASSERT_NE(nullptr, bus);
    shell::ShellEnvironment environment {&compositor, &manager, &seat, bus};
    environment.servicesSettings = directory.filePath("services.conf");
    shell.reset();
    shell = std::make_unique<shell::Shell>(environment);
    auto apps = std::make_unique<test::FakeApplications>();
    apps->addSimple("files", "Files", "folder");
    apps->addSimple("terminal", "Terminal", "utilities-terminal");
    apps->scan();
    shell->setApplications(std::move(apps));
    shell->setTaskWindows(std::make_unique<test::FakeTaskWindows>());
    auto *dock = openDock("[Dock]\nPinned=files, terminal\n");
    test::roundTrip(bus);
    shell::DockIcon *files = dock->applicationIcon("files");
    ASSERT_NE(nullptr, files);
    EXPECT_EQ(nullptr, files->countBadge());

    test::DBusTestClient client;
    client.sendLauncherEntry("application://files.desktop",
                             dbus::VariantMap {{"count", dbus::Variant(int64_t(7))},
                                               {"count-visible", dbus::Variant(true)}});
    ASSERT_TRUE(test::runUntil([files] { return files->countBadge() && files->countBadge()->isVisible(); }));
    shell::CountBadge *badge = files->countBadge();
    EXPECT_EQ("7", badge->text());
    EXPECT_EQ(nullptr, dock->applicationIcon("terminal")->countBadge());
    // A circle a third of the icon high, 4 past the icon's top and right edges.
    const ui::RectF icon = files->iconRect();
    EXPECT_EQ(ui::RectF(icon.x + icon.width + 4 - 18, icon.y - 4, 18, 18), badge->geometry());
    frame();

    // Two digits widen it into a pill, "99+" a little more; its corner stays.
    client.sendLauncherEntry("application://files.desktop", dbus::VariantMap {{"count", dbus::Variant(int64_t(42))}});
    ASSERT_TRUE(test::runUntil([badge] { return badge->count() == 42; }));
    EXPECT_EQ("42", badge->text());
    const double twoDigits = badge->geometry().width;
    EXPECT_GT(twoDigits, 18);
    EXPECT_EQ(18, badge->geometry().height);
    EXPECT_EQ(icon.x + icon.width + 4, badge->geometry().right());
    client.sendLauncherEntry("application://files.desktop", dbus::VariantMap {{"count", dbus::Variant(int64_t(120))}});
    ASSERT_TRUE(test::runUntil([badge] { return badge->count() == 120; }));
    EXPECT_EQ("99+", badge->text());
    EXPECT_GT(badge->geometry().width, twoDigits);
    EXPECT_EQ(icon.x + icon.width + 4, badge->geometry().right());
    frame();

    client.sendLauncherEntry("application://files.desktop",
                             dbus::VariantMap {{"count-visible", dbus::Variant(false)}});
    ASSERT_TRUE(test::runUntil([badge] { return !badge->isVisible(); }));
}

TEST_F(ShellComponentsTest, ADismissedMenuGivesTheTooltipBack)
{
    auto *dock = openDock("[Dock]\nPinned=files, terminal\n");
    const base::Rect window = dock->window()->geometry();
    const auto centre = [&](shell::DockIcon *slot) {
        const ui::RectF rect = slot->iconRect();
        return base::PointF(window.x() + slot->geometry().x + rect.x + rect.width / 2,
                            window.y() + slot->geometry().y + rect.y + rect.height / 2);
    };
    const base::PointF files = centre(dock->applicationIcon("files"));
    const base::PointF terminal = centre(dock->applicationIcon("terminal"));
    for (int round = 0; round < 2; ++round) {
        const int t = 100 * round;
        click(t + 1, files.x(), files.y(), BTN_RIGHT);
        ASSERT_NE(nullptr, dock->openMenu());
        frame();
        if (round == 0) {
            // Escape, as the capture script's settle() does.
            input.onKey(at(t + 5), KEY_ESC, true);
            input.onKey(at(t + 6), KEY_ESC, false);
        } else {
            // A press elsewhere.
            click(t + 5, 700, 300, BTN_LEFT);
        }
        test::processPendingEvents();
        frame();
        EXPECT_EQ(nullptr, dock->openMenu()) << round;
        EXPECT_FALSE(dock->applicationIcon("files")->isDimmed()) << round;
        move(t + 10, 700, 300);
        move(t + 11, terminal.x(), terminal.y());
        EXPECT_TRUE(dock->tooltip()->isShown()) << round;
        EXPECT_EQ("Terminal", dock->tooltip()->text()) << round;
    }
}

TEST_F(ShellComponentsTest, ADraggedIconMovesAmongThePinsAndOffTheDockUnpins)
{
    auto *dock = openDock("[Dock]\nPinned=files, terminal\n");
    input::DragController *drag = seat.drag();
    const base::PointF files = centreOf(dock, dock->applicationIcon("files"));
    const base::PointF terminal = centreOf(dock, dock->applicationIcon("terminal"));
    // The launcher stays where it is.
    beginDrag(1, centreOf(dock, dock->applicationIcon(shell::DockModel::kLauncherId)), {files.x(), files.y() - 40});
    EXPECT_FALSE(drag->active());
    release(5);

    // The terminal held before the files: its slot closes, one opens there.
    beginDrag(10, terminal, {terminal.x(), terminal.y() - 30});
    ASSERT_TRUE(drag->active());
    EXPECT_EQ(dock->window()->surface(), drag->origin());
    shell::DockIcon *icon = dock->applicationIcon("terminal");
    EXPECT_TRUE(icon->isDragHidden());
    EXPECT_FALSE(icon->isDimmed());
    shell::DockPresentation *presentation = dock->applicationPresentation();
    EXPECT_TRUE(presentation->isDragging());
    EXPECT_EQ(2, presentation->removedRow());
    move(20, files.x() - 20, files.y());
    EXPECT_TRUE(test::runUntil([&] { return presentation->placeholder() == 1; }));
    EXPECT_TRUE(presentation->placeholderPinned());
    EXPECT_TRUE(drag->targetAcceptsDrop());
    release(21);
    EXPECT_FALSE(drag->busy());
    EXPECT_EQ(dockIds(dock), (std::vector<std::string>{shell::DockModel::kLauncherId, "terminal", "files"}));
    EXPECT_FALSE(presentation->isDragging());
    EXPECT_FALSE(icon->isDragHidden());

    // Held off the dock by more than one and a half thicknesses it fades,
    // and is unpinned where it is let go.
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return !presentation->isRunning();
    }));
    const base::PointF moved = centreOf(dock, dock->applicationIcon("files"));
    beginDrag(30, moved, {moved.x(), moved.y() - 60});
    ASSERT_TRUE(drag->active());
    move(40, moved.x(), moved.y() - 200);
    release(41);
    EXPECT_FALSE(drag->busy());
    EXPECT_EQ(dockIds(dock), (std::vector<std::string>{shell::DockModel::kLauncherId, "terminal"}));
}

TEST_F(ShellComponentsTest, AnIconThatFinishedLeavingIsAnnouncedOnTheFrameThatDropsIt)
{
    // Driven by hand, at chosen frame times.
    class Presentation : public shell::DockPresentation
    {
    public:
        using shell::DockPresentation::DockPresentation;
        using shell::DockPresentation::tick;
    };

    auto *dock = openDock("[Dock]\nPinned=files\n");
    Presentation presentation(dock->window()->window());
    presentation.setKeys({"files", "terminal"});
    presentation.setAnimationsEnabled(true);
    int changes = 0;
    presentation.onChanged = [&] { ++changes; };
    presentation.setKeys({"files"});
    ASSERT_EQ(2u, presentation.entries().size());

    // The first frame backdates the start by one interval. The quintic ease
    // reaches its end in floating point a fraction of a millisecond early,
    // so the last frame moves nothing; it still drops the icon and says so.
    const auto ns = [](double ms) { return uint64_t(ms * 1e6); };
    const double start = 1000 - shell::DockPresentation::kFrameIntervalMs;
    presentation.tick(ns(1000));
    presentation.tick(ns(start + shell::DockPresentation::kDurationMs - 0.0001));
    ASSERT_EQ(2u, presentation.entries().size());
    const int before = changes;
    presentation.tick(ns(start + shell::DockPresentation::kDurationMs + 1));
    EXPECT_EQ(1u, presentation.entries().size());
    EXPECT_EQ(before + 1, changes);
}

TEST_F(ShellComponentsTest, ALeavingIconFadesWhereItIs)
{
    class Presentation : public shell::DockPresentation
    {
    public:
        using shell::DockPresentation::DockPresentation;
        using shell::DockPresentation::tick;
    };

    auto *dock = openDock("[Dock]\nPinned=files\n");
    Presentation presentation(dock->window()->window());
    presentation.setKeys({"files", "terminal", "editor"});
    presentation.setAnimationsEnabled(true);
    const auto ns = [](double ms) { return uint64_t(ms * 1e6); };
    const auto entry = [&](const std::string &key) {
        for (const shell::DockPresentation::Entry &e : presentation.entries()) {
            if (e.key == key)
                return e;
        }
        return shell::DockPresentation::Entry {};
    };

    // Taken out of the middle, it fades in its slot as the dock shrinks and
    // the next icon slides over it.
    presentation.setKeys({"files", "editor"});
    const double terminalAt = presentation.position(entry("terminal"));
    presentation.tick(ns(1000));
    const double halfway = 1000 + shell::DockPresentation::kDurationMs / 2;
    presentation.tick(ns(halfway));
    EXPECT_EQ(terminalAt, presentation.position(entry("terminal")));
    EXPECT_GT(presentation.appearance(entry("terminal")), 0);
    EXPECT_LT(presentation.appearance(entry("terminal")), 1);
    EXPECT_LT(presentation.length(), 3 * 70);
    EXPECT_LT(presentation.position(entry("editor")), 2 * 70);
    presentation.tick(ns(1000 + shell::DockPresentation::kDurationMs));
    EXPECT_EQ(2u, presentation.entries().size());
}

TEST_F(ShellComponentsTest, ANewIconGrowsAndFadesInShortlyAfterItsSlotStartsOpening)
{
    class Presentation : public shell::DockPresentation
    {
    public:
        using shell::DockPresentation::DockPresentation;
        using shell::DockPresentation::tick;
    };

    auto *dock = openDock("[Dock]\nPinned=files\n");
    Presentation presentation(dock->window()->window());
    presentation.setKeys({"files", "editor"});
    presentation.setAnimationsEnabled(true);
    presentation.setOpensForNewIcons(true);
    const auto ns = [](double ms) { return uint64_t(ms * 1e6); };
    const auto entry = [&](const std::string &key) {
        for (const shell::DockPresentation::Entry &e : presentation.entries()) {
            if (e.key == key)
                return e;
        }
        return shell::DockPresentation::Entry {};
    };

    // Hidden while its slot, opening more gently than other changes, starts.
    presentation.setKeys({"files", "editor", "terminal"});
    const double start = 1000 - shell::DockPresentation::kFrameIntervalMs;
    const double reveal = start + shell::DockPresentation::kRevealDelayMs;
    presentation.tick(ns(1000));
    presentation.tick(ns(reveal - 1));
    EXPECT_EQ(0, presentation.appearance(entry("terminal")));
    EXPECT_EQ(0, presentation.iconScale(entry("terminal")));
    EXPECT_GT(presentation.length(), 2 * 70);

    // Then its size and opacity grow together on OutQuint.
    double last = 0;
    for (double ms = 1000.0 / 60; ms < shell::DockPresentation::kRevealDurationMs - 10; ms += 1000.0 / 60) {
        presentation.tick(ns(reveal + ms));
        const double scale = presentation.iconScale(entry("terminal"));
        EXPECT_EQ(scale, presentation.appearance(entry("terminal")));
        EXPECT_GT(scale, last);
        EXPECT_LT(scale, 1);
        last = scale;
    }
    presentation.tick(ns(reveal + shell::DockPresentation::kRevealDurationMs / 2));
    EXPECT_NEAR(1 - std::pow(0.5, 5), presentation.iconScale(entry("terminal")), 1e-6);
    presentation.tick(ns(start + shell::DockPresentation::kOpeningDurationMs + 1));
    EXPECT_EQ(3 * 70, presentation.length());
    presentation.tick(ns(reveal + shell::DockPresentation::kRevealDurationMs + 1));
    EXPECT_EQ(1, presentation.iconScale(entry("terminal")));
    EXPECT_FALSE(presentation.isRunning());

    // One taken out while it grows fades out from the opacity it had.
    presentation.setKeys({"files", "editor", "calendar"});
    presentation.tick(ns(2000));
    presentation.tick(ns(2000 + shell::DockPresentation::kRevealDelayMs + 30));
    const double shown = presentation.appearance(entry("calendar"));
    EXPECT_GT(shown, 0);
    EXPECT_LT(shown, 1);
    presentation.setKeys({"files", "editor"});
    EXPECT_EQ(shown, presentation.appearance(entry("calendar")));
    EXPECT_EQ(1, presentation.iconScale(entry("calendar")));
}

TEST_F(ShellComponentsTest, ADraggedIconNothingTakesGoesBackToItsSlot)
{
    auto *dock = openDock("[Dock]\nPinned=files, terminal\n");
    input::DragController *drag = seat.drag();
    const base::PointF files = centreOf(dock, dock->applicationIcon("files"));
    // Not far enough off the dock to unpin, over nothing that takes it.
    beginDrag(1, files, {files.x(), files.y() - 30});
    ASSERT_TRUE(drag->active());
    move(10, files.x(), files.y() - 100);
    release(11);
    EXPECT_FALSE(drag->active());
    // With no icon to slide, it is home at once and shown in its slot again.
    EXPECT_FALSE(dock->isDraggingIcon());
    EXPECT_FALSE(dock->applicationIcon("files")->isDragHidden());
    EXPECT_FALSE(dock->applicationPresentation()->isDragging());
    EXPECT_EQ(dockIds(dock), (std::vector<std::string>{shell::DockModel::kLauncherId, "files", "terminal"}));
}

TEST_F(ShellComponentsTest, TheLastPinTakenOffTheDockOpensItsSlotAgainWhereItWas)
{
    auto *dock = openDock("[Dock]\nPinned=files, terminal\n");
    input::DragController *drag = seat.drag();
    const base::PointF terminal = centreOf(dock, dock->applicationIcon("terminal"));
    shell::DockPresentation *presentation = dock->applicationPresentation();

    // Off the dock its slot closes and the dock shrinks.
    beginDrag(1, terminal, {terminal.x(), terminal.y() - 30});
    ASSERT_TRUE(drag->active());
    move(10, terminal.x(), terminal.y() - 200);
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return presentation->placeholder() < 0 && !presentation->isRunning();
    }));

    // Back where it was, its slot opens again.
    move(20, terminal.x(), terminal.y());
    EXPECT_TRUE(test::runUntil([&] { return presentation->placeholder() == 2; }));
    EXPECT_TRUE(presentation->placeholderPinned());
    EXPECT_TRUE(drag->targetAcceptsDrop());

    // And over the divider gap, up to the trash.
    move(30, terminal.x(), terminal.y() - 200);
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return presentation->placeholder() < 0 && !presentation->isRunning();
    }));
    shell::DockIcon *trash = dock->trashIcon();
    const double trashStart = dock->window()->geometry().x() + trash->geometry().x + trash->iconRect().x;
    move(40, trashStart - 2, terminal.y());
    EXPECT_TRUE(test::runUntil([&] { return presentation->placeholder() == 2; }));
    EXPECT_TRUE(presentation->placeholderPinned());
    EXPECT_TRUE(drag->targetAcceptsDrop());
    release(41);
    EXPECT_EQ(dockIds(dock), (std::vector<std::string>{shell::DockModel::kLauncherId, "files", "terminal"}));
}

TEST_F(ShellComponentsTest, EscapeLeavesThePinsAsTheyWere)
{
    auto *dock = openDock("[Dock]\nPinned=files, terminal\n");
    input::DragController *drag = seat.drag();
    const base::PointF files = centreOf(dock, dock->applicationIcon("files"));
    const base::PointF terminal = centreOf(dock, dock->applicationIcon("terminal"));
    shell::DockPresentation *presentation = dock->applicationPresentation();

    // Held before the files, then off the dock far enough to unpin.
    beginDrag(1, terminal, {terminal.x(), terminal.y() - 30});
    ASSERT_TRUE(drag->active());
    move(10, files.x() - 20, files.y());
    ASSERT_TRUE(test::runUntil([&] { return presentation->placeholder() == 1; }));
    move(20, files.x(), files.y() - 200);
    input.onKey(at(21), KEY_ESC, true);
    input.onKey(at(22), KEY_ESC, false);
    EXPECT_FALSE(drag->active());
    release(23);
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return !dock->isDraggingIcon();
    }));
    EXPECT_EQ(dockIds(dock), (std::vector<std::string>{shell::DockModel::kLauncherId, "files", "terminal"}));
    EXPECT_FALSE(dock->applicationIcon("terminal")->isDragHidden());
    EXPECT_FALSE(presentation->isDragging());
}

TEST_F(ShellComponentsTest, TheLaunchersDragPinsWhereItIsDropped)
{
    applications->addSimple("editor", "Editor", "accessories-text-editor");
    applications->scan();
    auto *dock = openDock("[Dock]\nPinned=files, terminal\n");
    DragSourceView *view = nullptr;
    auto launcher = sourceWindow(shell::DockWindow::kDesktopMime, "editor", &view);
    input::DragController *drag = seat.drag();
    const base::PointF terminal = centreOf(dock, dock->applicationIcon("terminal"));

    beginDrag(1, {50, 10}, {terminal.x(), terminal.y() - 300});
    ASSERT_TRUE(drag->active());
    move(10, terminal.x() - 20, terminal.y());
    // The desktop id is read from the drag before a slot opens.
    shell::DockPresentation *presentation = dock->applicationPresentation();
    ASSERT_TRUE(test::runUntil([&] {
        move(11, terminal.x() - 21, terminal.y());
        move(12, terminal.x() - 20, terminal.y());
        return presentation->placeholder() >= 0;
    }));
    EXPECT_EQ(-1, presentation->removedRow());
    release(13);
    ASSERT_TRUE(test::runUntil([&] { return dockIds(dock).size() == 4; }));
    EXPECT_EQ(dockIds(dock), (std::vector<std::string>{shell::DockModel::kLauncherId, "files", "editor", "terminal"}));
    ASSERT_TRUE(test::runUntil([&] { return !view->finished.empty(); }));
    EXPECT_EQ(ui::MoveAction, view->finished.back());
    EXPECT_FALSE(presentation->isDragging());

    // Dropped nowhere on the dock: the dock is as it was.
    beginDrag(20, {50, 10}, {terminal.x(), terminal.y() - 300});
    move(30, terminal.x() - 20, terminal.y());
    test::runFor(std::chrono::milliseconds(50));
    move(31, 700, 300);
    release(32);
    EXPECT_FALSE(presentation->isDragging());
    EXPECT_EQ(dockIds(dock).size(), 4u);
}

TEST_F(ShellComponentsTest, AShellDragShowsItsIcon)
{
    scene::Node *icons = scene.root()->addChild(std::make_unique<scene::Node>("drag-icons"));
    seat.setDragIconContainer(icons);
    DragSourceView *view = nullptr;
    auto source = sourceWindow(shell::DockWindow::kDesktopMime, "files", &view);
    input::DragController *drag = seat.drag();

    beginDrag(1, {50, 10}, {400, 300});
    ASSERT_TRUE(drag->active());
    frame();
    ASSERT_EQ(1u, icons->children().size());
    auto *icon = dynamic_cast<scene::SurfaceNode *>(icons->children().front());
    ASSERT_NE(nullptr, icon);
    EXPECT_TRUE(icon->isEffectivelyVisible());
    EXPECT_EQ(base::Rect(396, 296, 8, 8), icon->geometry());
    // The picture the view gave, not an empty or a transparent one.
    auto *texture = dynamic_cast<testsupport::FakeTexture *>(icon->texture());
    ASSERT_NE(nullptr, texture);
    ASSERT_EQ(base::Size(8, 8), texture->size());
    const std::vector<uint8_t> &pixels = texture->pixels();
    for (size_t i = 0; i < pixels.size(); i += 4) {
        ASSERT_EQ(0xff, pixels[i + 3]) << "pixel " << i / 4;
        ASSERT_EQ(0x33, pixels[i]) << "pixel " << i / 4;
        ASSERT_EQ(0xcc, pixels[i + 2]) << "pixel " << i / 4;
    }

    release(10);
    EXPECT_FALSE(drag->busy());
}

TEST_F(ShellComponentsTest, TheTrashTakesFilesAndDimsUnderADrag)
{
    auto *dock = openDock("[Dock]\nPinned=files\n");
    DragSourceView *view = nullptr;
    auto manager = sourceWindow("text/uri-list", "file:///tmp/p4b-infra-a\n", &view);
    shell::DockIcon *trash = dock->trashIcon();
    const base::PointF centre = centreOf(dock, trash);
    beginDrag(1, {50, 10}, {centre.x(), centre.y() - 300});
    move(10, centre.x(), centre.y());
    EXPECT_TRUE(trash->isDropHover());
    EXPECT_TRUE(seat.drag()->targetAcceptsDrop());
    move(11, centre.x() - 200, centre.y());
    EXPECT_FALSE(trash->isDropHover());
    move(12, centre.x(), centre.y());
    release(13);
    EXPECT_FALSE(trash->isDropHover());
    ASSERT_TRUE(test::runUntil([&] { return !view->finished.empty(); }));
    EXPECT_EQ(ui::MoveAction, view->finished.back());
}

TEST_F(ShellComponentsTest, AFileHeldOverARunningApplicationRaisesIt)
{
    auto *dock = openDock("[Dock]\nPinned=files\n");
    shell::TaskWindow terminal;
    terminal.appId = "terminal";
    terminal.title = "Terminal";
    const uint64_t id = windows->add(terminal);
    ASSERT_TRUE(test::runUntil([dock] { return dock->applicationIcon("terminal"); }));
    test::runFor(std::chrono::milliseconds(400));
    frame();
    DragSourceView *view = nullptr;
    auto manager = sourceWindow("text/uri-list", "file:///tmp/p4b-infra-a\n", &view);
    const base::PointF centre = centreOf(dock, dock->applicationIcon("terminal"));
    beginDrag(1, {50, 10}, {centre.x(), centre.y() - 300});
    move(10, centre.x(), centre.y());
    windows->requests.clear();
    test::runFor(std::chrono::milliseconds(200));
    EXPECT_TRUE(windows->requests.empty());
    ASSERT_TRUE(test::runUntil([&] { return !windows->requests.empty(); }));
    EXPECT_EQ("activate " + std::to_string(id), windows->requests.back());
    move(11, 700, 300);
    release(12);
}

TEST_F(ShellComponentsTest, MinimizedWindowsSwapUnderADragAndTheirBadgeFadesIn)
{
    auto *dock = openDock("[Dock]\nPinned=files\n");
    std::vector<uint64_t> ids;
    for (const char *title : {"One", "Two"}) {
        shell::TaskWindow window;
        window.appId = "terminal";
        window.title = title;
        window.geometry = base::Rect(100, 100, 400, 200);
        const uint64_t id = windows->add(window);
        windows->m_snapshots[id] = ui::Bitmap(base::Size(40, 20), ui::Bitmap::Format::Argb32Premultiplied);
        windows->change(id, [](shell::TaskWindow &w) { w.minimized = true; }, shell::kStateChange);
        ids.push_back(id);
    }
    shell::MinimizedWindows &minimized = dock->dock().minimizedWindows();
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return dock->minimizedIcon(ids[0]) && dock->minimizedIcon(ids[1]) && minimized.entries()[0].landed
                && minimized.entries()[1].landed && !dock->minimizedPresentation()->isRunning();
    }));
    // Landed: the badge fades in over 140 ms rather than at once.
    shell::DockIcon *first = dock->minimizedIcon(ids[0]);
    ASSERT_TRUE(first->hasThumbnail());
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return first->badgeOpacity() == 1;
    }));

    const base::PointF from = centreOf(dock, first);
    const base::PointF to = centreOf(dock, dock->minimizedIcon(ids[1]));
    beginDrag(1, from, {from.x(), from.y() - 30});
    ASSERT_TRUE(seat.drag()->active());
    EXPECT_TRUE(first->isDragHidden());
    move(10, to.x(), to.y());
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return minimized.entries()[0].id == ids[1];
    }));
    release(11);
    EXPECT_FALSE(first->isDragHidden());
    EXPECT_EQ(ids[0], minimized.entries()[1].id);
}

TEST_F(ShellComponentsTest, AMinimizedWindowClosedUnderADragLeavesNothingHovered)
{
    auto *dock = openDock("[Dock]\nPinned=files\n");
    std::vector<uint64_t> ids;
    for (const char *title : {"One", "Two"}) {
        shell::TaskWindow window;
        window.appId = "terminal";
        window.title = title;
        window.geometry = base::Rect(100, 100, 400, 200);
        const uint64_t id = windows->add(window);
        windows->m_snapshots[id] = ui::Bitmap(base::Size(40, 20), ui::Bitmap::Format::Argb32Premultiplied);
        windows->change(id, [](shell::TaskWindow &w) { w.minimized = true; }, shell::kStateChange);
        ids.push_back(id);
    }
    shell::MinimizedWindows &minimized = dock->dock().minimizedWindows();
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return dock->minimizedIcon(ids[0]) && dock->minimizedIcon(ids[1]) && minimized.entries()[0].landed
                && minimized.entries()[1].landed && !dock->minimizedPresentation()->isRunning();
    }));

    // Held over the second, which closes before the swap: its icon goes
    // while the drag still rests where it was.
    const base::PointF from = centreOf(dock, dock->minimizedIcon(ids[0]));
    const base::PointF to = centreOf(dock, dock->minimizedIcon(ids[1]));
    beginDrag(1, from, {from.x(), from.y() - 30});
    ASSERT_TRUE(seat.drag()->active());
    move(10, to.x(), to.y());
    test::processPendingEvents();
    windows->remove(ids[1]);
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return !dock->minimizedIcon(ids[1]) && !dock->minimizedPresentation()->isRunning();
    }));
    test::runFor(std::chrono::milliseconds(300));
    release(11);
    ASSERT_EQ(1, minimized.count());
    EXPECT_EQ(ids[0], minimized.entries()[0].id);
    EXPECT_FALSE(dock->minimizedIcon(ids[0])->isDragHidden());
}

TEST_F(ShellComponentsTest, AClosedMinimizedWindowFadesOutAsItsThumbnail)
{
    auto *dock = openDock("[Dock]\nPinned=files\n");
    shell::TaskWindow window;
    window.appId = "terminal";
    window.title = "One";
    window.geometry = base::Rect(100, 100, 400, 200);
    window.minimized = true;
    const uint64_t id = windows->add(window);
    windows->m_snapshots[id] = ui::Bitmap(base::Size(40, 20), ui::Bitmap::Format::Argb32Premultiplied);
    windows->minimizeLanded(id);
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        return !dock->minimizedPresentation()->isRunning() && !dock->applicationPresentation()->isRunning();
    }));
    shell::DockIcon *icon = dock->minimizedIcon(id);
    ASSERT_NE(nullptr, icon);
    ASSERT_TRUE(icon->hasThumbnail());
    const std::string iconName = icon->iconName();

    // Its image and its application go with the window, before the dock lets
    // the slot go: what leaves is still the thumbnail, not a stand-in icon.
    windows->m_snapshots.erase(id);
    windows->remove(id);
    bool faded = false;
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        if (shell::DockIcon *leaving = dock->minimizedIcon(id)) {
            EXPECT_TRUE(leaving->hasThumbnail());
            EXPECT_EQ(iconName, leaving->iconName());
            faded = faded || (leaving->presentationOpacity() > 0 && leaving->presentationOpacity() < 1);
        }
        return !dock->minimizedIcon(id);
    }));
    EXPECT_TRUE(faded);
}

TEST_F(ShellComponentsTest, AMinimizingWindowAimsAtWhereItsThumbnailComesToRest)
{
    auto *dock = openDock("[Dock]\nPinned=files\n");
    shell::TaskWindow window;
    window.appId = "terminal";
    window.title = "One";
    window.geometry = base::Rect(100, 100, 400, 200);
    const uint64_t id = windows->add(window);
    windows->m_snapshots[id] = ui::Bitmap(base::Size(40, 20), ui::Bitmap::Format::Argb32Premultiplied);
    frame();
    const base::Rect aimed = dock->minimizeTarget(id);
    ASSERT_FALSE(aimed.isEmpty());

    // The slot opens and the dock recentres under the flying window without
    // moving where it lands.
    windows->change(id, [](shell::TaskWindow &w) { w.minimized = true; }, shell::kStateChange);
    bool opening = false;
    ASSERT_TRUE(test::runUntil([&] {
        frame();
        EXPECT_EQ(dock->minimizeTarget(id), aimed);
        // The window itself is what flies in; the slot only opens.
        if (shell::DockIcon *icon = dock->minimizedIcon(id))
            EXPECT_EQ(icon->presentationOpacity(), 0);
        opening = opening || dock->minimizedPresentation()->isRunning();
        return opening && !dock->minimizedPresentation()->isRunning();
    }));

    // Landed, it becomes its thumbnail whole, which follows the slot and sits
    // where it was aimed.
    windows->minimizeLanded(id);
    frame();
    ASSERT_TRUE(dock->dock().minimizedWindows().entries()[0].landed);
    shell::DockIcon *icon = dock->minimizedIcon(id);
    EXPECT_EQ(icon->presentationOpacity(), 1);
    EXPECT_EQ(icon->presentationScale(), 1);
    EXPECT_EQ(dock->minimizeTarget(id), aimed);

    // Restored, the window flies out of the slot itself.
    windows->change(id, [](shell::TaskWindow &w) { w.minimized = false; }, shell::kStateChange);
    frame();
    EXPECT_EQ(icon->presentationOpacity(), 0);
}

TEST_F(ShellComponentsTest, AClosedShellWindowLeavesTheSceneWithTheNextFrame)
{
    scene.render(renderer);
    const size_t before = surfaceContainer->children().size();
    std::vector<std::unique_ptr<shell::ShellWindow>> panels;
    for (int i = 0; i < 3; ++i) {
        panels.push_back(std::make_unique<shell::ShellWindow>(shell.get(), wm::WindowRole::Panel));
        panels.back()->setContentView(std::make_unique<ui::View>());
        panels.back()->setGeometry(base::Rect(0, 0, 200, kStatusBar));
        panels.back()->show();
    }
    test::processPendingEvents();
    frame();
    scene.render(renderer);

    panels.clear();
    test::processPendingEvents();
    frame();
    // Each window's node stays until a frame collects it.
    EXPECT_EQ(before + 3, surfaceContainer->children().size());

    ASSERT_TRUE(scene.hasDamage(renderer.outputContext()));
    scene.render(renderer);
    EXPECT_EQ(before, surfaceContainer->children().size());
}
