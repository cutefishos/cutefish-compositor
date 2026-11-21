// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>

#include <gtest/gtest.h>

#include "base/application_registry.h"
#include "core/main_loop.h"
#include "dbus_test_client.h"
#include "shell/common/application_badges.h"
#include "shell/common/count_badge.h"
#include "shell/common/file_theme_icon.h"
#include "shell/common/task_manager.h"
#include "shell/common/text_match.h"
#include "shell_test_util.h"
#include "temporary_directory.h"
#include "toolkit/toolkit_host.h"
#include "ui/bitmap.h"
#include "ui/embedded_window.h"

using namespace cutefish;

TEST(TextMatchTest, FoldsCaseAsQtDoes)
{
    EXPECT_TRUE(shell::containsIgnoringCase("Terminal", "TERM"));
    EXPECT_TRUE(shell::containsIgnoringCase("\xc3\x89" "cran", "\xc3\xa9" "c"));
    EXPECT_TRUE(shell::containsIgnoringCase("\xce\xa3\xce\xb5", "\xcf\x83"));
    EXPECT_TRUE(shell::containsIgnoringCase("anything", ""));
    EXPECT_FALSE(shell::containsIgnoringCase("Files", "fz"));
}

namespace {

class TaskManagerTest : public testing::Test
{
protected:
    TaskManagerTest()
    {
        applications.addSimple("org.example.Editor", "Editor", "editor", "StartupWMClass=editor-window\n");
        applications.addSimple("org.example.Viewer", "Viewer");
    }

    std::unique_ptr<shell::TaskManager> create()
    {
        return std::make_unique<shell::TaskManager>(
                windows, applications, [this](int64_t pid) { return commands[pid]; },
                [this](int64_t pid) { return parents[pid]; });
    }

    shell::TaskWindow window(const std::string &appId, const std::string &title, int64_t pid = 0)
    {
        shell::TaskWindow w;
        w.appId = appId;
        w.title = title;
        w.pid = pid;
        return w;
    }

    test::FakeApplications applications;
    test::FakeTaskWindows windows;
    std::map<int64_t, shell::ProcessCommand> commands;
    std::map<int64_t, int64_t> parents;
};

} // namespace

TEST_F(TaskManagerTest, AnnouncesApplicationsOnlyOnceTheFirstScanIsIn)
{
    auto tasks = create();
    std::vector<std::string> added;
    auto subscription = tasks->applicationAdded.connect([&](const std::string &a) { added.push_back(a); });

    windows.add(window("editor-window", "Draft"));
    EXPECT_FALSE(tasks->isReady());
    EXPECT_TRUE(added.empty());

    bool ready = false;
    auto readySubscription = tasks->readyChanged.connect([&] { ready = true; });
    applications.scan();
    EXPECT_TRUE(ready);
    EXPECT_TRUE(added.empty());
    EXPECT_EQ(tasks->applications(), (std::vector<std::string>{"org.example.Editor"}));

    windows.add(window("org.example.Viewer", "Picture"));
    EXPECT_EQ(added, (std::vector<std::string>{"org.example.Viewer"}));
}

TEST_F(TaskManagerTest, GroupsWindowsAndFollowsChanges)
{
    applications.scan();
    auto tasks = create();
    std::vector<std::string> events;
    auto added = tasks->applicationAdded.connect([&](const std::string &a) { events.push_back("+" + a); });
    auto removed = tasks->applicationRemoved.connect([&](const std::string &a) { events.push_back("-" + a); });
    auto changed = tasks->applicationChanged.connect([&](const std::string &a) { events.push_back("~" + a); });

    const uint64_t first = windows.add(window("org.example.Editor", "One"));
    const uint64_t second = windows.add(window("org.example.Editor", "Two"));
    const uint64_t loose = windows.add(window("unknown-app", "Loose"));
    EXPECT_EQ(tasks->windows("org.example.Editor"), (std::vector<uint64_t>{first, second}));
    EXPECT_EQ(tasks->entry("unknown-app"), nullptr);
    EXPECT_EQ(tasks->applicationOf(loose), "unknown-app");

    events.clear();
    windows.change(first, [](shell::TaskWindow &w) { w.title = "One!"; }, shell::kTitleChange);
    windows.change(loose, [](shell::TaskWindow &w) { w.title = "Loose!"; }, shell::kTitleChange);
    EXPECT_EQ(events, (std::vector<std::string>{"~unknown-app"}));

    events.clear();
    windows.change(loose, [](shell::TaskWindow &w) { w.skipTaskbar = true; }, shell::kSkipTaskbarChange);
    EXPECT_EQ(events, (std::vector<std::string>{"-unknown-app"}));
    EXPECT_EQ(tasks->applicationOf(loose), "");

    events.clear();
    windows.change(second, [](shell::TaskWindow &w) { w.appId = "org.example.Viewer"; }, shell::kAppIdChange);
    EXPECT_EQ(events, (std::vector<std::string>{"~org.example.Editor", "+org.example.Viewer"}));

    events.clear();
    windows.remove(first);
    EXPECT_EQ(events, (std::vector<std::string>{"-org.example.Editor"}));
    EXPECT_EQ(tasks->applications(), (std::vector<std::string>{"org.example.Viewer"}));
}

TEST_F(TaskManagerTest, KeepsAWindowWithItsEntryThroughARescan)
{
    applications.scan();
    auto tasks = create();
    const uint64_t id = windows.add(window("org.example.Editor", "One"));
    std::vector<std::string> events;
    auto changed = tasks->applicationChanged.connect([&](const std::string &a) { events.push_back(a); });

    applications.addSimple("org.example.Editor", "Editor", "editor", "StartupWMClass=editor-window\n");
    applications.scan();
    EXPECT_TRUE(events.empty());

    applications.addSimple("org.example.Editor", "Better Editor", "editor");
    applications.scan();
    EXPECT_EQ(events, (std::vector<std::string>{"org.example.Editor"}));
    EXPECT_EQ(tasks->entryForWindow(id)->name, "Better Editor");
}

TEST_F(TaskManagerTest, FindsTheEntryOfAProcessThroughItsAncestors)
{
    applications.scan();
    auto tasks = create();
    windows.add(window("org.example.Viewer", "Main", 100));
    parents[105] = 101;
    parents[101] = 100;
    EXPECT_EQ(tasks->entryForProcess(105)->id, "org.example.Viewer");

    tasks->claimProcess(101, "org.example.Editor");
    EXPECT_EQ(tasks->entryForProcess(105)->id, "org.example.Editor");
    tasks->releaseProcess(101);

    commands[200] = {"/usr/bin/org.example.Editor", "org.example.Editor"};
    EXPECT_EQ(tasks->resolveProcess({}, 200, {})->id, "org.example.Editor");
    EXPECT_EQ(tasks->entryForProcess(300), nullptr);
}

static std::string infoPlist(const std::string &name)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>"
           "<key>CFBundleName</key><string>" + name + "</string>"
           "<key>CFBundleIdentifier</key><string>org.example." + name + "</string>"
           "</dict></plist>\n";
}

static std::vector<std::string> names(const shell::Applications &applications)
{
    std::vector<std::string> list;
    for (const shell::DesktopEntryPtr &entry : applications.applications())
        list.push_back(entry->name);
    return list;
}

TEST(RegistryApplicationsTest, ListsInstalledBundlesWhileNothingRunsThem)
{
    const test::TemporaryDirectory directory;
    ASSERT_TRUE(directory.write("applications/files.desktop",
                                "[Desktop Entry]\nType=Application\nName=Files\nExec=files\n"));
    ASSERT_TRUE(directory.write("Applications/Editor.app/Contents/Info.plist", infoPlist("Editor")));
    ASSERT_TRUE(directory.write("Applications/Zebra.app/Contents/Info.plist", infoPlist("Zebra")));

    base::EventLoop &loop = *core::mainLoop();
    base::ApplicationRegistry::setInstance(
            loop, base::ApplicationRegistry::create(loop, {directory.filePath("applications")},
                                                    {directory.filePath("Applications")}));
    {
        shell::RegistryApplications applications;
        ASSERT_TRUE(test::runUntil(
                [&] { return !applications.isLoading() && applications.applications().size() == 3; }));
        EXPECT_EQ(names(applications), (std::vector<std::string>{"Editor", "Files", "Zebra"}));
        const shell::DesktopEntryPtr editor = applications.byPath(directory.filePath("Applications/Editor.app"));
        ASSERT_TRUE(editor);
        EXPECT_TRUE(editor->exec.empty());

        // Once something runs them, they are listed once, with its command.
        ASSERT_TRUE(directory.write("applications/runner.desktop",
                                    "[Desktop Entry]\nType=Application\nName=Runner\nExec=runner %f\n"
                                    "MimeType=application/x-apple-app;\nNoDisplay=true\n"));
        ASSERT_TRUE(test::runUntil([&] {
            const shell::DesktopEntryPtr entry = applications.byPath(directory.filePath("Applications/Editor.app"));
            return entry && !entry->exec.empty();
        }));
        EXPECT_EQ(names(applications), (std::vector<std::string>{"Editor", "Files", "Zebra"}));
    }
    base::ApplicationRegistry::setInstance(loop, nullptr);
}

TEST(ApplicationBadgesTest, NamesTheDesktopIdOfALauncherUri)
{
    EXPECT_EQ(shell::ApplicationBadges::desktopId("application://org.telegram.desktop.desktop"),
              "org.telegram.desktop");
    EXPECT_EQ(shell::ApplicationBadges::desktopId("application://code"), "code");
    EXPECT_EQ(shell::ApplicationBadges::desktopId("file:///usr/share/applications/code.desktop"), "");
}

TEST(ApplicationBadgesTest, ShowsUpTo99)
{
    EXPECT_EQ(shell::CountBadge::textFor(0), "");
    EXPECT_EQ(shell::CountBadge::textFor(1), "1");
    EXPECT_EQ(shell::CountBadge::textFor(99), "99");
    EXPECT_EQ(shell::CountBadge::textFor(100), "99+");
    EXPECT_EQ(shell::CountBadge::textFor(9999), "99+");
}

TEST(ApplicationBadgesTest, FollowsTheCountsApplicationsSend)
{
    namespace dbus = base::dbus;
    dbus::Connection *bus = dbus::Connection::session(core::mainLoop());
    ASSERT_NE(nullptr, bus);
    shell::ApplicationBadges badges(bus);
    int changes = 0;
    const base::Subscription changed = badges.changed.connect([&changes] { ++changes; });
    test::roundTrip(bus);

    auto client = std::make_unique<test::DBusTestClient>();
    client->sendLauncherEntry("application://org.example.Chat.desktop",
                              dbus::VariantMap {{"count", dbus::Variant(int64_t(7))},
                                                {"count-visible", dbus::Variant(true)}});
    ASSERT_TRUE(test::runUntil([&] { return badges.count("org.example.Chat") == 7; }));
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(badges.count("org.example.chat"), 7);
    EXPECT_EQ(badges.count("org.example.Other"), 0);

    client->sendLauncherEntry("application://org.example.Chat.desktop",
                              dbus::VariantMap {{"count", dbus::Variant(int64_t(120))}});
    ASSERT_TRUE(test::runUntil([&] { return badges.count("org.example.Chat") == 120; }));
    client->sendLauncherEntry("application://org.example.Chat.desktop",
                              dbus::VariantMap {{"count-visible", dbus::Variant(false)}});
    ASSERT_TRUE(test::runUntil([&] { return badges.count("org.example.Chat") == 0; }));
    client->sendLauncherEntry("application://org.example.Chat.desktop",
                              dbus::VariantMap {{"count-visible", dbus::Variant(true)}});
    ASSERT_TRUE(test::runUntil([&] { return badges.count("org.example.Chat") == 120; }));
    client->sendLauncherEntry("application://org.example.Chat.desktop",
                              dbus::VariantMap {{"count", dbus::Variant(int64_t(0))}});
    ASSERT_TRUE(test::runUntil([&] { return badges.count("org.example.Chat") == 0; }));
    client->sendLauncherEntry("application://org.example.Chat.desktop",
                              dbus::VariantMap {{"count", dbus::Variant(int64_t(3))}});
    ASSERT_TRUE(test::runUntil([&] { return badges.count("org.example.Chat") == 3; }));
    changes = 0;

    client.reset();
    ASSERT_TRUE(test::runUntil([&] { return badges.count("org.example.Chat") == 0; }));
    EXPECT_EQ(changes, 1);
}

namespace {

uint8_t centreAlpha(const ui::EmbeddedWindow::Frame &frame)
{
    const int x = frame.width / 2;
    const int y = frame.height / 2;
    return frame.pixels[size_t(y) * frame.stride + size_t(x) * 4 + 3];
}

} // namespace

TEST(FileThemeIconTest, ReadsItsFileOnThePoolAndDrawsOnceItIsThere)
{
    toolkit::application();
    const std::string directory = testing::TempDir() + "file-theme-icon";
    std::filesystem::create_directories(directory);
    const std::string file = directory + "/AppIcon.png";
    ui::Bitmap art(32, 32, ui::Bitmap::Format::Argb32Premultiplied);
    art.fill(0xff3385ff);
    ASSERT_TRUE(art.savePng(file));

    ui::EmbeddedWindow window;
    auto content = std::make_unique<shell::FileThemeIcon>(file, 32);
    shell::FileThemeIcon *icon = content.get();
    window.setContentView(std::move(content));
    window.setSize({32, 32});
    EXPECT_EQ(centreAlpha(window.render()), 0);
    ASSERT_TRUE(test::runUntil([&window] { return window.needsRender(); }));
    EXPECT_EQ(centreAlpha(window.render()), 255);

    // A file whose reads hang, as on a stuck mount, never holds up a frame.
    const std::string fifo = directory + "/Stuck.png";
    ASSERT_EQ(mkfifo(fifo.c_str(), 0600), 0);
    icon->setIconName(fifo);
    EXPECT_EQ(centreAlpha(window.render()), 0);
    EXPECT_FALSE(shell::FileThemeIcon::isReady(fifo, 32, 1, {}));
    // Lets the pool's read go: a writer opens once the read waits.
    int writer = -1;
    ASSERT_TRUE(test::runUntil([&] {
        writer = open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
        return writer >= 0;
    }));
    close(writer);
    ASSERT_TRUE(test::runUntil([&] { return shell::FileThemeIcon::isReady(fifo, 32, 1, {}); }));
    EXPECT_EQ(centreAlpha(window.render()), 0);
    std::filesystem::remove_all(directory);
}
