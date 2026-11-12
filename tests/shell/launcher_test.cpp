// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "base/file.h"
#include "base/settings.h"
#include "base/strings.h"
#include "dbus_test_client.h"
#include "loop_test_util.h"
#include "shell/common/app_launcher.h"
#include "shell/common/file_thread.h"
#include "shell/common/task_manager.h"
#include "shell/launcher/launcher.h"
#include "shell_test_util.h"
#include "temporary_directory.h"

using namespace cutefish;
namespace dbus = base::dbus;

namespace {

class LauncherTest : public testing::Test
{
protected:
    LauncherTest()
    {
        applications.addSimple("zeta", "Zeta");
        applications.addSimple("alpha", "Alpha", "", "GenericName=Web Browser\n");
        applications.addSimple("mid", "Middle", "", "Comment=\xc3\x89" "cran\n");
        applications.addSimple("console", "Console", "", "Terminal=true\n");
    }

    void start(const std::string &settings = {})
    {
        if (!settings.empty())
            directory.write("shell.conf", settings);
        launcher = std::make_unique<shell::Launcher>(dbus::Connection::session(core::mainLoop()), tasks, starter,
                                                     nullptr, directory.filePath("shell.conf"));
    }

    shell::LauncherModel &model() { return launcher->applications(); }

    std::vector<std::string> ids()
    {
        std::vector<std::string> list;
        for (const auto &item : model().rows())
            list.push_back(item.id);
        return list;
    }

    test::TemporaryDirectory directory;
    test::FakeApplications applications;
    test::FakeTaskWindows windows;
    shell::TaskManager tasks{windows, applications, [](int64_t) { return shell::ProcessCommand(); }};
    shell::ApplicationLauncher starter{nullptr};
    std::unique_ptr<shell::Launcher> launcher;
};

} // namespace

TEST_F(LauncherTest, SortsTheFirstListAndKeepsTheUsersOrderAfter)
{
    start();
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 3; }));
    // Alphabetical in the user's language; terminal programs are left out.
    EXPECT_EQ(ids(), (std::vector<std::string>{"alpha", "mid", "zeta"}));
    EXPECT_FALSE(model().rows()[0].newInstalled);

    model().move(2, 0);
    EXPECT_EQ(ids(), (std::vector<std::string>{"zeta", "alpha", "mid"}));
    model().save();
    shell::waitForFileThread();

    const base::Settings settings(directory.filePath("shell.conf"));
    EXPECT_EQ(settings.stringListValue("Launcher/order"), (std::vector<std::string>{"zeta", "alpha", "mid"}));
    EXPECT_TRUE(settings.contains("Launcher/newAppIds"));

    // A new installation is appended and marked new.
    applications.addSimple("beta", "Beta");
    applications.scan();
    EXPECT_EQ(ids(), (std::vector<std::string>{"zeta", "alpha", "mid", "beta"}));
    EXPECT_TRUE(model().rows()[3].newInstalled);
    model().clearNewInstalled({"beta"});
    EXPECT_FALSE(model().rows()[3].newInstalled);
}

TEST_F(LauncherTest, ReadsTheStoredOrder)
{
    start("[Launcher]\norder=zeta, gone, mid\nnewAppIds=mid\n");
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 3; }));
    EXPECT_EQ(ids(), (std::vector<std::string>{"zeta", "mid", "alpha"}));
    EXPECT_TRUE(model().rows()[1].newInstalled);
    EXPECT_TRUE(model().rows()[2].newInstalled);
    EXPECT_EQ(model().rows()[2].name, "Alpha");
}

TEST_F(LauncherTest, AnUnknownListIsAFirstStart)
{
    start("[Launcher]\nlist=@ByteArray(\\0\\0\\0\\x1)\n");
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 3; }));
    EXPECT_EQ(ids(), (std::vector<std::string>{"alpha", "mid", "zeta"}));
    EXPECT_FALSE(model().rows()[0].newInstalled);
}

TEST_F(LauncherTest, SearchesNamesCommentsAndIds)
{
    start();
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 3; }));
    model().search("  browser ");
    EXPECT_EQ(ids(), (std::vector<std::string>{"alpha"}));
    model().search("\xc3\xa9" "c");
    EXPECT_EQ(ids(), (std::vector<std::string>{"mid"}));
    // No moving while a search shows.
    model().move(0, 1);
    model().search("");
    EXPECT_EQ(ids(), (std::vector<std::string>{"alpha", "mid", "zeta"}));
}

TEST_F(LauncherTest, FoldersHoldApplicationsAndAreSaved)
{
    start();
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 3; }));
    const auto children = [this](const std::string &folder) {
        std::vector<std::string> list;
        if (const shell::LauncherItem *item = model().item(folder)) {
            for (const auto &child : item->children)
                list.push_back(child.id);
        }
        return list;
    };

    // Dropped on another application, the two make a folder in its place.
    const std::string folder = model().merge("zeta", "alpha", "Work");
    EXPECT_EQ(folder, "folder:1");
    EXPECT_EQ(ids(), (std::vector<std::string>{"folder:1", "mid"}));
    EXPECT_EQ(children(folder), (std::vector<std::string>{"alpha", "zeta"}));
    EXPECT_EQ(model().item(folder)->name, "Work");
    EXPECT_EQ(model().folderOf("zeta"), folder);
    EXPECT_EQ(model().findById("zeta"), -1);
    // A folder does not go into another, and an application dropped on its
    // own folder stays where it is.
    EXPECT_EQ(model().merge(folder, "mid", "Other"), "");
    EXPECT_EQ(model().merge("zeta", folder, {}), "");
    EXPECT_EQ(children(folder), (std::vector<std::string>{"alpha", "zeta"}));

    // A search looks into the folders and finds applications only.
    model().search("zeta");
    EXPECT_EQ(ids(), (std::vector<std::string>{"zeta"}));
    model().search("");

    model().renameFolder(folder, "Games");
    model().moveInFolder(folder, 1, 0);
    EXPECT_EQ(children(folder), (std::vector<std::string>{"zeta", "alpha"}));
    model().save();
    shell::waitForFileThread();
    {
        const base::Settings settings(directory.filePath("shell.conf"));
        EXPECT_EQ(settings.stringListValue("Launcher/order"), (std::vector<std::string>{"folder:1", "mid"}));
        EXPECT_EQ(settings.stringValue("Launcher/Folder1/name"), "Games");
        EXPECT_EQ(settings.stringListValue("Launcher/Folder1/apps"), (std::vector<std::string>{"zeta", "alpha"}));
    }

    // Read back as it was written.
    launcher.reset();
    start();
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 2; }));
    EXPECT_EQ(ids(), (std::vector<std::string>{"folder:1", "mid"}));
    EXPECT_EQ(children("folder:1"), (std::vector<std::string>{"zeta", "alpha"}));
    EXPECT_EQ(model().item("folder:1")->name, "Games");
    EXPECT_EQ(model().rows()[0].children[0].name, "Zeta");
}

TEST_F(LauncherTest, AFolderLeftWithOneApplicationGivesWayToIt)
{
    start("[Launcher]\norder=folder:4, mid\nFolder4/name=Tools\nFolder4/apps=alpha, gone, zeta\n");
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 2; }));
    // Applications no longer installed leave it.
    ASSERT_EQ(model().item("folder:4")->children.size(), 2u);

    // Out of the folder, just after it; the one left takes the folder's
    // place.
    model().removeFromFolder("alpha");
    EXPECT_EQ(ids(), (std::vector<std::string>{"zeta", "alpha", "mid"}));
    EXPECT_EQ(model().item("folder:4"), nullptr);

    // Made from the menu, a folder holds one application; ungrouped, its
    // applications take its place.
    EXPECT_EQ(model().createFolder("alpha", "Folder"), "folder:1");
    model().merge("mid", "folder:1", {});
    EXPECT_EQ(ids(), (std::vector<std::string>{"zeta", "folder:1"}));
    EXPECT_EQ(model().merge("zeta", "folder:1", {}), "folder:1");
    EXPECT_EQ(model().createFolder("folder:1", "Inner"), "");
    model().ungroupFolder("folder:1");
    EXPECT_EQ(ids(), (std::vector<std::string>{"alpha", "mid", "zeta"}));

    // An application uninstalled from a folder of two leaves the other in
    // its place at the next login; until then the folder waits for it.
    model().merge("zeta", "mid", "Pair");
    EXPECT_EQ(ids(), (std::vector<std::string>{"alpha", "folder:1"}));
    applications.remove("zeta");
    applications.scan();
    EXPECT_EQ(ids(), (std::vector<std::string>{"alpha", "folder:1"}));
    ASSERT_EQ(model().item("folder:1")->children.size(), 1u);
    model().save();
    shell::waitForFileThread();
    launcher.reset();
    start();
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 2; }));
    EXPECT_EQ(ids(), (std::vector<std::string>{"alpha", "mid"}));
}

TEST_F(LauncherTest, AnEntryGoneForAMomentKeepsItsPlace)
{
    applications.addSimple("beta", "Beta");
    applications.addSimple("gamma", "Gamma");
    start("[Launcher]\norder=zeta, folder:1, mid, folder:2\nFolder1/name=Games\nFolder1/apps=alpha, beta\n"
          "Folder2/name=Tools\nFolder2/apps=gamma\n");
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 4; }));
    const auto children = [this](const std::string &folder) {
        std::vector<std::string> list;
        if (const shell::LauncherItem *item = model().item(folder)) {
            for (const auto &child : item->children)
                list.push_back(child.id);
        }
        return list;
    };

    // As a package upgrade replaces the entries: they leave the rows, a
    // folder of two keeps the other, and an emptied folder hides.
    applications.remove("alpha");
    applications.remove("zeta");
    applications.remove("gamma");
    applications.scan();
    EXPECT_EQ(ids(), (std::vector<std::string>{"folder:1", "mid"}));
    EXPECT_EQ(children("folder:1"), (std::vector<std::string>{"beta"}));

    // Saved in their places meanwhile.
    model().save();
    shell::waitForFileThread();
    {
        const base::Settings settings(directory.filePath("shell.conf"));
        EXPECT_EQ(settings.stringListValue("Launcher/order"),
                  (std::vector<std::string>{"zeta", "folder:1", "mid", "folder:2"}));
        EXPECT_EQ(settings.stringListValue("Launcher/Folder1/apps"), (std::vector<std::string>{"alpha", "beta"}));
        EXPECT_EQ(settings.stringListValue("Launcher/Folder2/apps"), (std::vector<std::string>{"gamma"}));
    }

    // A new folder takes a number not held by the hidden one.
    EXPECT_EQ(model().createFolder("mid", "New"), "folder:3");
    model().ungroupFolder("folder:3");

    applications.addSimple("gamma", "Gamma");
    applications.addSimple("zeta", "Zeta");
    applications.scan();
    applications.addSimple("alpha", "Alpha");
    applications.scan();
    EXPECT_EQ(ids(), (std::vector<std::string>{"zeta", "folder:1", "mid", "folder:2"}));
    EXPECT_EQ(children("folder:1"), (std::vector<std::string>{"alpha", "beta"}));
    EXPECT_EQ(children("folder:2"), (std::vector<std::string>{"gamma"}));
    EXPECT_EQ(model().item("folder:2")->name, "Tools");
    for (const shell::LauncherItem &item : model().allItems()) {
        EXPECT_FALSE(item.newInstalled) << item.id;
        for (const shell::LauncherItem &child : item.children)
            EXPECT_FALSE(child.newInstalled) << child.id;
    }
}

TEST_F(LauncherTest, NeighboursGoneTogetherComeBackInOrder)
{
    applications.addSimple("beta", "Beta");
    start("[Launcher]\norder=alpha, beta, mid, zeta\n");
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 4; }));

    applications.remove("beta");
    applications.scan();
    applications.remove("mid");
    applications.scan();
    EXPECT_EQ(ids(), (std::vector<std::string>{"alpha", "zeta"}));
    // Moved meanwhile, each still follows the one it followed.
    model().move(0, 1);
    applications.addSimple("mid", "Middle");
    applications.scan();
    EXPECT_EQ(ids(), (std::vector<std::string>{"zeta", "alpha", "mid"}));
    applications.addSimple("beta", "Beta");
    applications.scan();
    EXPECT_EQ(ids(), (std::vector<std::string>{"zeta", "alpha", "beta", "mid"}));
}

TEST_F(LauncherTest, PagesFollowTheList)
{
    start();
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return model().count() == 3; }));
    shell::LauncherPageModel page(model());
    page.setLimitCount(2);
    EXPECT_EQ(page.count(), 2);
    EXPECT_EQ(page.item(0)->id, "alpha");
    page.setStartIndex(2);
    EXPECT_EQ(page.count(), 1);
    EXPECT_EQ(page.item(0)->id, "zeta");
    EXPECT_EQ(page.sourceRow(0), 2);

    page.setStartIndex(0);
    int moves = 0;
    auto subscription = page.rowMoved.connect([&](int, int) { ++moves; });
    model().move(1, 0);
    EXPECT_EQ(page.item(0)->id, "mid");
    EXPECT_EQ(moves, 1);
}

TEST_F(LauncherTest, ShowsHidesAndRemembersOpenedApplications)
{
    applications.scan();
    start();
    ASSERT_TRUE(launcher->registerService());
    int shown = 0;
    launcher->showWindow = [&] { ++shown; };
    launcher->hideWindow = [&] { --shown; };

    test::DBusTestClient session;
    session.call("org.cutefish.Launcher", "/Launcher", "org.cutefish.Launcher", "toggle");
    EXPECT_TRUE(launcher->isShown());
    EXPECT_EQ(shown, 1);
    // A drag keeps it up when it loses activation.
    launcher->setDragging(true);
    launcher->deactivated();
    EXPECT_TRUE(launcher->isShown());
    launcher->setDragging(false);
    launcher->deactivated();
    EXPECT_FALSE(launcher->isShown());

    shell::TaskWindow window;
    window.appId = "zeta";
    windows.add(window);
    EXPECT_EQ(launcher->takeOpenedApplications(), (std::vector<std::string>{"zeta"}));
    EXPECT_TRUE(launcher->takeOpenedApplications().empty());
}

// A search for "te" over a typical set of applications, in the en_SG locale:
// name, generic name, comment and desktop id, as substrings ignoring case,
// in the user's order.
TEST(LauncherSearchTest, MatchesTheQtLauncherOnTheReferenceApplications)
{
    struct App {
        const char *id;
        const char *name;
        const char *genericName;
        const char *comment;
    };
    const App apps[] = {
        {"io.bassi.Amberol", "Amberol", "Music Player", ""},
        {"chromium", "Chromium Web Browser", "Web Browser", "Access the Internet"},
        {"org.gnome.dfeet", "D-Feet", "D-Bus Debugger", "Debug D-Bus applications"},
        {"cutefish-filemanager", "Files", "File Manager", "Browse and manage your files"},
        {"com.mitchellh.ghostty", "Ghostty", "", "A terminal emulator"},
        {"google-chrome", "Google Chrome", "Web Browser", "Access the Internet"},
        {"org.gnome.Loupe", "Image Viewer", "", ""},
        {"libreoffice-startcenter", "LibreOffice", "Office",
         "Launch applications to create text documents, spreadsheets, presentations, drawings, formulas, and "
         "databases, or open recently used documents."},
        {"libreoffice-calc", "LibreOffice Calc", "Spreadsheet",
         "Perform calculations, analyze information and manage lists in spreadsheets."},
        {"libreoffice-draw", "LibreOffice Draw", "Drawing Program", "Create and edit drawings, flow charts and logos."},
        {"libreoffice-impress", "LibreOffice Impress", "Presentation",
         "Create and edit presentations for slideshows, meetings and Web pages."},
        {"libreoffice-math", "LibreOffice Math", "Formula Editor", "Create and edit scientific formulas and equations."},
        {"libreoffice-writer", "LibreOffice Writer", "Word Processor",
         "Create and edit text and graphics in letters, reports, documents and Web pages."},
        {"com.obsproject.Studio", "OBS Studio", "Streaming/Recording Software",
         "Free and Open Source Streaming/Recording Software"},
        {"cutefish-screenshot", "Screenshot", "", "Capture or record the screen, a window or a selected portion"},
        {"cutefish-settings", "Settings", "", ""},
        {"dev.lizardbyte.app.Sunshine", "Sunshine", "", "Self-hosted game stream host for Moonlight"},
        {"cutefish-terminal", "Terminal", "", ""},
        {"org.gnome.TextEditor", "Text Editor", "Text Editor", "View and edit text files"},
        {"vlc", "VLC media player", "Media player", "Read, capture, broadcast your multimedia streams"},
        {"cutefish-system-monitor", "System Monitor", "", "View resource usage and manage running processes"},
        {"cutefish-video-player", "Video Player", "", "Play videos and other media files"},
        {"cutefish-text-editor", "Text Editor", "", "Edit plain text files"},
        {"org.keepassxc.KeePassXC", "KeePassXC", "Password Manager",
         "Community-driven port of the Windows application \xe2\x80\x9cKeePass Password Safe\xe2\x80\x9d"},
        {"cutefish-calendar", "Calendar", "", "Keep track of events and appointments"},
        {"org.gnome.Cheese", "Cheese", "Webcam Booth", "Take photos and videos with your webcam, with fun graphical effects"},
        {"org.gnome.SoundRecorder", "Sound Recorder", "", "Record sound via the microphone and play it back"},
        {"ai.storyteller.photocraft", "PhotoCraft", "Image Editor", "Edit photos and layered PSD documents"},
        {"firefox", "Firefox", "Web Browser", "Fast and private browser"},
    };
    test::TemporaryDirectory directory;
    test::FakeApplications applications;
    std::string order;
    for (const App &app : apps) {
        std::string extra;
        if (*app.genericName)
            extra += std::string("GenericName=") + app.genericName + "\n";
        if (*app.comment)
            extra += std::string("Comment=") + app.comment + "\n";
        applications.addSimple(app.id, app.name, "", extra);
        order += std::string(order.empty() ? "" : ", ") + app.id;
    }
    directory.write("shell.conf", "[Launcher]\norder=" + order + "\n");
    test::FakeTaskWindows windows;
    shell::TaskManager tasks{windows, applications, [](int64_t) { return shell::ProcessCommand(); }};
    shell::ApplicationLauncher starter{nullptr};
    shell::Launcher launcher(nullptr, tasks, starter, nullptr, directory.filePath("shell.conf"));
    applications.scan();
    ASSERT_TRUE(test::runUntil([&] { return launcher.applications().count() == int(std::size(apps)); }));

    launcher.applications().search("te");
    std::vector<std::string> found;
    for (const auto &item : launcher.applications().rows())
        found.push_back(item.id);
    EXPECT_EQ(found, (std::vector<std::string>{
                             "chromium", "cutefish-filemanager", "com.mitchellh.ghostty", "google-chrome",
                             "libreoffice-startcenter", "libreoffice-draw", "libreoffice-impress", "libreoffice-math",
                             "libreoffice-writer", "cutefish-screenshot", "cutefish-settings",
                             "dev.lizardbyte.app.Sunshine", "cutefish-terminal", "org.gnome.TextEditor",
                             "cutefish-system-monitor", "cutefish-video-player", "cutefish-text-editor",
                             "cutefish-calendar", "ai.storyteller.photocraft", "firefox"}));
}
