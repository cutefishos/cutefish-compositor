// SPDX-License-Identifier: BSD-3-Clause
#include <chrono>

#include <linux/input-event-codes.h>
#include <gtest/gtest.h>

#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "fake_renderer.h"
#include "input/input_manager.h"
#include "input/seat.h"
#include "loop_test_util.h"
#include "scene/scene.h"
#include "shell/clipboard/clipboard_history.h"
#include "shell/spotlight/spotlight.h"
#include "shell/spotlight/spotlight_search.h"
#include "shell/shell_test_util.h"
#include "temporary_directory.h"
#include "wm/window.h"
#include "wm/window_manager.h"

using namespace cutefish;
using namespace cutefish::shell::spotlight;

TEST(SpotlightSearch, RanksNamesAndCombinesCategoryWithQuery)
{
    auto photos = std::make_shared<base::DesktopEntry>();
    photos->name = "Photos";
    photos->categories = {"Graphics"};
    auto other = std::make_shared<base::DesktopEntry>();
    other->name = "Viewer";
    other->keywords = {"photos"};
    const std::vector<shell::DesktopEntryPtr> apps{other, photos};
    EXPECT_EQ(findApps(apps, " PHOTOS "), (std::vector<shell::DesktopEntryPtr>{photos, other}));
    EXPECT_EQ(findApps(apps, "ph", Category::Creativity), (std::vector<shell::DesktopEntryPtr>{photos}));
    EXPECT_TRUE(findApps(apps, "ph", Category::Social).empty());
}

TEST(SpotlightSearch, FiltersTypesAndFolderWithoutFollowingLinks)
{
    test::TemporaryDirectory dir;
    const auto album = dir.filePath("Iceland Scans");
    std::filesystem::create_directory(album);
    dir.write("Iceland Scans/DSC06514.ARW", "raw");
    dir.write("Iceland Scans/DSC06514.jpg", "jpeg");
    dir.write("DSC06514.jpg", "unrelated");
    dir.write(".private.txt", "hidden");
    std::filesystem::create_directory_symlink(album, dir.filePath("Album link"));
    const auto files = indexFiles(dir.filePath(""));
    EXPECT_EQ(findFiles(files, "dsc06514", FileType::Images, album, 10).size(), 2u);
    EXPECT_EQ(findFiles(files, "dsc06514", FileType::All, {}, 10).size(), 3u);
    EXPECT_EQ(findFiles(files, "iceland", FileType::Folders, {}, 10).size(), 1u);
    EXPECT_TRUE(findFiles(files, "private", FileType::All, {}, 10).empty());
    EXPECT_TRUE(findFiles(files, "album link", FileType::All, {}, 10).empty());
}

namespace {

class SpotlightWindowTest : public testing::Test
{
protected:
    scene::Scene scene;
    compositor::Compositor compositor{scene.root()->addChild(std::make_unique<scene::Node>("surfaces"))};
    testsupport::FakeRenderer renderer;
    input::Seat seat{&compositor};
    input::InputManager input{&seat};
    wm::WindowManager wm{&compositor, &seat};
    test::TemporaryDirectory dir;
    std::unique_ptr<shell::Shell> desktop;
    shell::Spotlight *spotlight = nullptr;
    shell::ClipboardHistory *clipboard = nullptr;
    int clock = 100;

    void SetUp() override
    {
        const base::Rect bounds(0, 0, 1440, 960);
        scene.setSize(bounds.size());
        wm.setOutputBounds(bounds);
        input.setBounds(bounds.size());
        input.setEventSink(&wm);
        shell::ShellEnvironment env{&compositor, &wm, &seat};
        env.servicesSettings = dir.filePath("services.conf");
        desktop = std::make_unique<shell::Shell>(env);
        auto apps = std::make_unique<test::FakeApplications>();
        for (int i = 0; i < 24; ++i)
            apps->addSimple("app" + std::to_string(i), "Application " + std::to_string(i), "application-x-executable");
        apps->scan();
        desktop->setApplications(std::move(apps));
        desktop->setTaskWindows(std::make_unique<test::FakeTaskWindows>());
        auto history = std::make_unique<shell::ClipboardHistory>(nullptr, *seat.selection(), dir.filePath("clipboard"));
        clipboard = history.get();
        spotlight = desktop->addComponent(std::make_unique<shell::Spotlight>(desktop.get(), nullptr,
                std::move(history), dir.filePath("spotlight.conf")));
    }

    void TearDown() override
    {
        desktop.reset();
        seat.shutdown();
    }

    void render() { compositor.updateTextures(renderer); }

    wm::Window *picker() const
    {
        for (auto *window : wm.windows()) {
            if (!window->isPopup() && window->role() == wm::WindowRole::Picker)
                return window;
        }
        return nullptr;
    }

    void key(uint32_t code, bool pressed)
    {
        input.onKey(input::Timestamp(std::chrono::milliseconds(clock++)), code, pressed);
    }

    void tap(uint32_t code) { key(code, true); key(code, false); }

    void click(double x, double y)
    {
        input.onPointerMotionAbsolute(input::Timestamp(std::chrono::milliseconds(clock++)), {x, y});
        input.onPointerButton(input::Timestamp(std::chrono::milliseconds(clock++)), BTN_LEFT, true);
        input.onPointerButton(input::Timestamp(std::chrono::milliseconds(clock++)), BTN_LEFT, false);
    }
};

} // namespace

TEST_F(SpotlightWindowTest, IsFloatingAndSwitchesModesWithKeysAndMouse)
{
    using Mode = shell::Spotlight::Mode;
    spotlight->show(Mode::Search);
    render();
    ASSERT_NE(picker(), nullptr);
    EXPECT_EQ(picker()->bounds().width(), 620);
    EXPECT_EQ(picker()->bounds().height(), 54);
    EXPECT_EQ(picker()->rootSurface(), seat.focusedSurface());
    ASSERT_TRUE(test::runUntil([this] { return picker()->bounds().y() == 211; }));
    const auto collapsed = picker()->bounds();
    click(collapsed.right() - 212, collapsed.y() + 26);
    ASSERT_TRUE(test::runUntil([this] { return picker() && picker()->bounds().height() > 300; }));
    render();
    EXPECT_EQ(picker()->bounds().x(), collapsed.x());
    key(KEY_LEFTMETA, true);
    tap(KEY_4);
    key(KEY_LEFTMETA, false);
    ASSERT_TRUE(test::runUntil([this] { return picker() && picker()->bounds().height() == 114; }));
    render();
    EXPECT_EQ(picker()->rootSurface(), seat.focusedSurface());
    tap(KEY_ESC);
    EXPECT_EQ(picker(), nullptr);
    EXPECT_EQ(seat.focusedSurface(), nullptr);
}

TEST_F(SpotlightWindowTest, ClipboardDiscardAndDisableDoesNotResurrectPendingCopy)
{
    ASSERT_TRUE(test::runUntil([this] { return clipboard->store().isLoaded(); }));
    ASSERT_TRUE(clipboard->store().add({{"text/plain", "do not retain"}}));
    clipboard->store().setEnabled(false);
    EXPECT_FALSE(clipboard->store().add({{"text/plain", "disabled"}}));
    test::runFor(std::chrono::milliseconds(60));
    EXPECT_TRUE(clipboard->store().entries().empty());
    clipboard->store().setEnabled(true);
    ASSERT_TRUE(clipboard->store().add({{"text/plain", "new entry"}}));
    ASSERT_TRUE(test::runUntil([this] { return clipboard->store().entries().size() == 1; }));
    EXPECT_EQ(clipboard->store().entries().front().text, "new entry");
}

TEST_F(SpotlightWindowTest, MenuKeepsPickerOpenAndOutsideClickDismissesIt)
{
    spotlight->show(shell::Spotlight::Mode::Applications);
    ASSERT_TRUE(test::runUntil([this] { return picker() && picker()->bounds().height() > 450; }));
    render();
    const auto bounds = picker()->bounds();
    click(bounds.right() - 28, bounds.y() + 26);
    render();
    const auto menuVisible = [this] {
        return std::any_of(wm.windows().begin(), wm.windows().end(), [](const wm::Window *window) {
            return window->isPopup() && window->isVisible();
        });
    };
    ASSERT_TRUE(test::runUntil(menuVisible));
    tap(KEY_ESC);
    EXPECT_TRUE(test::runUntil([&] { return !menuVisible(); }));
    ASSERT_NE(picker(), nullptr);
    click(20, 20);
    EXPECT_TRUE(test::runUntil([this] { return !picker(); }));
}

TEST_F(SpotlightWindowTest, CommandConventionSwitchesModeWithoutSendingDigitsToSearch)
{
    seat.setShortcutConvention(input::ShortcutConvention::Command);
    spotlight->show(shell::Spotlight::Mode::Search);
    render();
    key(KEY_LEFTALT, true); // The physical key next to Space is Command.
    tap(KEY_4);
    key(KEY_LEFTALT, false);
    ASSERT_TRUE(test::runUntil([this] { return picker() && picker()->bounds().height() == 114; }));
    // Toggling Clipboard closes it only if the chord actually took us there.
    spotlight->toggle(shell::Spotlight::Mode::Clipboard);
    EXPECT_EQ(picker(), nullptr);
}
