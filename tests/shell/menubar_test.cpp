// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The status bar's menu bar as the compositor draws it, in a bar of the
// test's own: entries, places, opening, switching, keys, the user's, the
// application's and the desktop's menus, on a private session bus.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <tuple>

#include <linux/input-event-codes.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "compositor/compositor.h"
#include "compositor/menu_model.h"
#include "compositor/surface.h"
#include "dbus_test_client.h"
#include "fake_renderer.h"
#include "input/input_manager.h"
#include "input/seat.h"
#include "loop_test_util.h"
#include "scene/node.h"
#include "scene/scene.h"
#include "shell/menubar/menu_bar_view.h"
#include "shell/shell.h"
#include "shell/shell_data.h"
#include "shell/shell_host.h"
#include "shell/shell_window.h"
#include "shell/statusbar/application_menu.h"
#include "shell/statusbar/bar_item.h"
#include "shell/statusbar/dbus_menu.h"
#include "shell/statusbar/status_bar.h"
#include "shell_test_util.h"
#include "temporary_directory.h"
#include "ui/embedded_window.h"
#include "ui/menu.h"
#include "wm/window.h"
#include "wm/window_manager.h"

using namespace cutefish;
namespace dbus = base::dbus;

namespace {

input::Timestamp at(int ms)
{
    return input::Timestamp(std::chrono::milliseconds(ms));
}

const base::Rect kOutput(0, 0, 1440, 960);

using Layout = std::tuple<int32_t, dbus::VariantMap, std::vector<dbus::Variant>>;

dbus::Variant layout(int32_t id, dbus::VariantMap properties, std::vector<dbus::Variant> children = {})
{
    return dbus::Variant(Layout(id, std::move(properties), std::move(children)));
}

// A main menu as a window gives it through cutefish_main_menu_v1, kept in
// memory; it records what the bar asks of it.
class FakeMainMenu : public compositor::MenuModel
{
public:
    FakeMainMenu()
    {
        m_items[0].hasSubmenu = true;
        m_items[0].childrenLoaded = true;
    }

    void add(int32_t parent, int32_t id, const std::string &label,
             compositor::MenuItem::Role role = compositor::MenuItem::Role::None)
    {
        compositor::MenuItem &item = m_items[id];
        item.id = id;
        item.parent = parent;
        item.label = label;
        item.role = role;
        compositor::MenuItem &above = m_items[parent];
        above.children.push_back(id);
        above.hasSubmenu = true;
        above.childrenLoaded = true;
    }

    void rename(int32_t id, const std::string &label)
    {
        m_items[id].label = label;
        itemChanged(id);
    }

    // As a main menu's client changes it: each insertion and removal on its
    // own, the removed entry still there while its menu hears.
    void insert(int32_t parent, int32_t id, const std::string &label)
    {
        add(parent, id, label);
        menuUpdated(parent);
    }

    void remove(int32_t id)
    {
        const int32_t parent = m_items[id].parent;
        std::erase(m_items[parent].children, id);
        menuUpdated(parent);
        m_items.erase(id);
    }

    int prepared(int32_t id) const { return int(std::count(m_prepared.begin(), m_prepared.end(), id)); }
    int closedCount(int32_t id) const { return int(std::count(m_closed.begin(), m_closed.end(), id)); }

    const compositor::MenuItem *item(int32_t id) const override
    {
        const auto it = m_items.find(id);
        return it == m_items.end() ? nullptr : &it->second;
    }
    void refresh(int32_t) override { }
    void prepare(int32_t id) override
    {
        m_prepared.push_back(id);
        menuUpdated(id);
    }
    void opened(int32_t) override { }
    void closed(int32_t id) override { m_closed.push_back(id); }
    void activate(int32_t id) override { activated.push_back(id); }

    std::vector<int32_t> activated;

private:
    std::map<int32_t, compositor::MenuItem> m_items;
    std::vector<int32_t> m_prepared;
    std::vector<int32_t> m_closed;
};

dbus::Variant label(const std::string &text)
{
    return dbus::Variant(text);
}

dbus::Variant submenu()
{
    return dbus::Variant(std::string("submenu"));
}

// A menu exporter of the test's own; every call and event is recorded.
class FakeMenuExporter
{
public:
    FakeMenuExporter(dbus::Connection *bus, const std::string &path)
        : object(bus, path, "com.canonical.dbusmenu")
    {
        object.addMethod("GetLayout", "iias", "u(ia{sv}av)", [this](dbus::MethodCall &call) {
            int32_t parent = 0;
            int32_t depth = 0;
            std::vector<std::string> names;
            call.read(parent, depth, names);
            calls.push_back("GetLayout " + std::to_string(parent));
            const auto it = layouts.find(parent);
            if (it == layouts.end()) {
                call.replyError("com.canonical.dbusmenu.Error", "No such item");
                return;
            }
            call.replyWith({dbus::Variant(revision), it->second});
        });
        object.addMethod("AboutToShow", [this](int32_t id) {
            calls.push_back("AboutToShow " + std::to_string(id));
            return false;
        });
        object.addMethod("Event", "isvu", "", [this](dbus::MethodCall &call) {
            int32_t id = 0;
            std::string event;
            call.read(id, event);
            calls.push_back("Event " + std::to_string(id) + " " + event);
            call.reply();
            if (onEvent)
                onEvent(id, event);
        });
        object.addSignal("LayoutUpdated", "ui");
        object.addSignal("ItemsPropertiesUpdated", "a(ia{sv})a(ias)");
        object.addSignal("ItemActivationRequested", "iu");
        EXPECT_TRUE(object.publish());
    }

    int count(const std::string &what) const { return int(std::count(calls.begin(), calls.end(), what)); }
    bool called(const std::string &what) const { return count(what) > 0; }
    // The "opened" events, whichever menu they were for.
    int openedEvents() const
    {
        return int(std::count_if(calls.begin(), calls.end(),
                                 [](const std::string &call) { return call.ends_with(" opened"); }));
    }

    // The application says the menu `parent` changed under it.
    void layoutUpdated(int32_t parent = 0) { object.emitSignal("LayoutUpdated", ++revision, parent); }

    dbus::Object object;
    std::map<int32_t, dbus::Variant> layouts;
    std::vector<std::string> calls;
    uint32_t revision = 1;
    // After an event is answered.
    std::function<void(int32_t id, const std::string &event)> onEvent;
};

// The desktop's File and Edit menus.
std::shared_ptr<FakeMainMenu> desktopMenus()
{
    auto menu = std::make_shared<FakeMainMenu>();
    menu->add(0, 1, "File");
    menu->add(1, 10, "New Window");
    menu->add(1, 11, "New Folder");
    menu->add(1, 13, "Properties");
    menu->add(0, 2, "Edit");
    menu->add(2, 20, "Undo");
    menu->add(2, 21, "Select All");
    return menu;
}

// The status bar's part the menu bar needs: a 32 px panel along the top, its
// slot from the left edge clipped to 1000 px, and a press elsewhere on the
// bar closing the menus.
class TestBar final : public shell::Component, public shell::BarContext
{
public:
    explicit TestBar(shell::Shell *shell)
    {
        window = std::make_unique<shell::ShellWindow>(shell, wm::WindowRole::Panel);
        window->setFocusPolicy(wm::FocusPolicy::NoFocus);
        window->window()->setBackgroundColor(ui::Color {0, 0, 0, 0});
        auto root = std::make_unique<Root>(this);
        this->root = root.get();
        slot = root->addChild<ui::View>();
        slot->setClipsChildren(true);
        window->setContentView(std::move(root));
        window->setGeometry({0, 0, kOutput.width(), 32});
        window->setExclusiveZone(wm::ScreenEdge::Top, 32);
        window->show();
    }

    ~TestBar() override
    {
        if (menuBar)
            std::unique_ptr<ui::View> gone = slot->takeChild(menuBar);
        window.reset();
    }

    void setMenuBar(std::unique_ptr<shell::StatusBarMenuBar> bar)
    {
        menuBar = bar.get();
        slot->addChild(std::move(bar));
        root->requestLayout();
    }

    shell::StatusBarMenuBarHost host(shell::StatusBar *statusBar)
    {
        shell::StatusBarMenuBarHost host;
        host.bar = this;
        host.statusBar = statusBar;
        host.closePanels = [this] { ++panelCloses; };
        host.closeTrayMenu = [this] { ++trayCloses; };
        host.availableGeometry = [] { return ui::RectF {0, 32, double(kOutput.width()), double(kOutput.height() - 32)}; };
        return host;
    }

    bool lightContents() const override { return light; }

    std::unique_ptr<shell::ShellWindow> window;
    ui::View *slot = nullptr;
    shell::StatusBarMenuBar *menuBar = nullptr;
    bool light = false;
    int panelCloses = 0;
    int trayCloses = 0;

private:
    class Root final : public ui::View
    {
    public:
        explicit Root(TestBar *bar)
            : m_bar(bar)
        {
        }

        bool acceptsPointer() const override { return true; }

    protected:
        void layout() override
        {
            m_bar->slot->setGeometry({0, 0, 1000, geometry().height});
            if (m_bar->menuBar)
                m_bar->menuBar->setGeometry({0, 0, 1000, geometry().height});
        }

        void pointerPress(const ui::PointerEvent &) override
        {
            if (m_bar->menuBar)
                m_bar->menuBar->closeMenu();
        }

    private:
        TestBar *m_bar;
    };

    Root *root = nullptr;
};

class MenuBarTest : public testing::Test
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
    std::unique_ptr<shell::StatusBar> statusBar;
    TestBar *bar = nullptr;
    shell::MenuBarView *view = nullptr;
    int time = 0;
    base::PointF lastClick {-100, -100};
    uint32_t lastButton = 0;

    void SetUp() override
    {
        scene.setSize(kOutput.size());
        manager.setOutputBounds(kOutput);
        input.setBounds(kOutput.size());
        input.setEventSink(&manager);
        shell::ShellEnvironment environment {&compositor, &manager, &seat};
        environment.bus = dbus::Connection::session(core::mainLoop());
        environment.servicesSettings = directory.filePath("services.conf");
        shell = std::make_unique<shell::Shell>(environment);
        auto apps = std::make_unique<test::FakeApplications>();
        applications = apps.get();
        applications->addSimple("terminal", "Terminal", "utilities-terminal");
        applications->scan();
        shell->setApplications(std::move(apps));
        auto tasks = std::make_unique<test::FakeTaskWindows>();
        windows = tasks.get();
        shell->setTaskWindows(std::move(tasks));
        statusBar = std::make_unique<shell::StatusBar>(environment.bus, nullptr, nullptr,
                                                       directory.filePath("shell.conf"));
        ASSERT_TRUE(statusBar->registerServices());
        bar = shell->addComponent(std::make_unique<TestBar>(shell.get()));
        auto menuBar = std::make_unique<shell::MenuBarView>(shell.get(), bar->host(statusBar.get()));
        view = menuBar.get();
        bar->setMenuBar(std::move(menuBar));
        frame();
    }

    void TearDown() override
    {
        shell.reset();
        statusBar.reset();
        seat.shutdown();
    }

    void frame()
    {
        test::processPendingEvents();
        compositor.updateTextures(renderer);
    }

    void move(double x, double y) { input.onPointerMotionAbsolute(at(time += 5), {x, y}); }

    void click(double x, double y, uint32_t button = BTN_LEFT)
    {
        // Two clicks on one spot within the toolkit's 400 ms are a double click.
        if (button == lastButton && std::abs(x - lastClick.x()) < 8 && std::abs(y - lastClick.y()) < 8)
            test::runFor(std::chrono::milliseconds(420));
        lastClick = {x, y};
        lastButton = button;
        move(x, y);
        input.onPointerButton(at(time += 5), button, true);
        input.onPointerButton(at(time += 5), button, false);
        frame();
    }

    void key(uint32_t code)
    {
        input.onKey(at(time += 5), code, true);
        input.onKey(at(time += 5), code, false);
        frame();
    }

    std::vector<std::string> titles() const
    {
        std::vector<std::string> result;
        for (const shell::MenuBarModel::Row &row : view->model().rows())
            result.push_back(row.title);
        return result;
    }

    // An entry's rect in the bar.
    ui::RectF entryRect(size_t index) const { return view->entry(index)->geometry(); }

    base::PointF entryCentre(size_t index) const
    {
        const ui::RectF rect = entryRect(index);
        return {rect.x + rect.width / 2, rect.y + rect.height / 2};
    }

    // The open menu's window as the window manager placed it.
    std::optional<base::Rect> menuBounds() const
    {
        ui::Menu *menu = view->openMenu();
        if (!menu || !menu->window())
            return std::nullopt;
        shell::ShellWindow *window =
                shell->host()->windowFor(static_cast<ui::EmbeddedWindow *>(menu->window()));
        if (!window || !window->wmWindow())
            return std::nullopt;
        return window->wmWindow()->bounds();
    }

    // The desktop as the file manager has it: a Desktop window whose surface
    // has the File and Edit main menu.
    std::unique_ptr<shell::ShellWindow> showDesktop(const std::shared_ptr<FakeMainMenu> &menu)
    {
        auto desktop = std::make_unique<shell::ShellWindow>(shell.get(), wm::WindowRole::Desktop);
        desktop->setGeometry(kOutput);
        desktop->surface()->setMainMenu(menu);
        desktop->show();
        EXPECT_TRUE(test::runUntil([this] { return view->model().count() == 4; }));
        frame();
        return desktop;
    }

    // A focused terminal window whose menus `application` exports at
    // /MenuBar, once the bar shows `count` entries.
    void showTerminal(test::DBusTestClient &application, size_t count)
    {
        shell::TaskWindow terminal;
        terminal.appId = "terminal";
        terminal.title = "Terminal window";
        terminal.pid = 4242;
        terminal.menuService = application.connection->uniqueName();
        terminal.menuPath = "/MenuBar";
        windows->setActive(windows->add(terminal));
        ASSERT_TRUE(test::runUntil([&] { return view->model().count() == count; }));
        frame();
    }

    // The first row of the open menu, chosen from the keyboard.
    void chooseFirstRow()
    {
        key(KEY_DOWN);
        key(KEY_ENTER);
    }
};

} // namespace

TEST_F(MenuBarTest, TheUserEntryAloneWithNothingFocusedAndNoDesktopMenu)
{
    // Nothing focused is the desktop: its name, with no menus of its own yet.
    EXPECT_TRUE(view->model().isDesktop());
    ASSERT_EQ(titles(), (std::vector<std::string> {"", "Desktop"}));
    EXPECT_EQ(shell::MenuBarModel::Kind::User, view->model().rows()[0].kind);
    EXPECT_EQ(shell::MenuBarModel::Kind::Application, view->model().rows()[1].kind);

    // The user's entry: 46 wide, its plate 9 from the screen edge.
    EXPECT_EQ(ui::RectF(0, 0, 46, 32), entryRect(0));
    EXPECT_EQ(9, view->entry(0)->leftInset());
    EXPECT_EQ(3, view->entry(0)->rightInset());
    // The next abuts it, 24 wider than its title.
    EXPECT_EQ(46, entryRect(1).x);
    EXPECT_EQ(3, view->entry(1)->leftInset());
}

TEST_F(MenuBarTest, TheUserMenuOpensUnderItsPlateAndClosesOnASecondClick)
{
    click(entryCentre(0).x(), 16);
    ASSERT_EQ(0, view->currentIndex());
    EXPECT_TRUE(view->entry(0)->isChecked());
    EXPECT_EQ(1, bar->panelCloses);
    EXPECT_EQ(1, bar->trayCloses);
    ui::Menu *menu = view->openMenu();
    ASSERT_NE(nullptr, menu);
    EXPECT_TRUE(menu->isOpen());
    // Plate's left edge, the popup margin under the bar.
    const auto bounds = menuBounds();
    ASSERT_TRUE(bounds);
    EXPECT_EQ(9, bounds->x());
    EXPECT_EQ(38, bounds->y());
    EXPECT_EQ(1u, manager.popupGrabs().size());

    // Again on the open entry: closed.
    click(entryCentre(0).x(), 16);
    EXPECT_EQ(-1, view->currentIndex());
    EXPECT_FALSE(menu->isOpen());
    EXPECT_FALSE(view->entry(0)->isChecked());
    EXPECT_TRUE(manager.popupGrabs().empty());

    // A second click soon enough to make a double click closes it too.
    click(entryCentre(0).x(), 16);
    ASSERT_EQ(0, view->currentIndex());
    input.onPointerButton(at(time += 5), BTN_LEFT, true);
    input.onPointerButton(at(time += 5), BTN_LEFT, false);
    frame();
    EXPECT_EQ(-1, view->currentIndex());
    EXPECT_FALSE(menu->isOpen());
    EXPECT_TRUE(manager.popupGrabs().empty());

    // The right button opens nothing.
    click(entryCentre(0).x(), 16, BTN_RIGHT);
    EXPECT_EQ(-1, view->currentIndex());

    // Escape closes it; so does a press on the bar between the entries.
    click(entryCentre(0).x(), 16);
    ASSERT_EQ(0, view->currentIndex());
    key(KEY_ESC);
    EXPECT_EQ(-1, view->currentIndex());
    EXPECT_FALSE(view->entry(0)->isChecked());
    click(entryCentre(0).x(), 16);
    ASSERT_EQ(0, view->currentIndex());
    click(1200, 16);
    EXPECT_EQ(-1, view->currentIndex());
    // A press outside the bar and the menus ends the grab.
    click(entryCentre(0).x(), 16);
    ASSERT_EQ(0, view->currentIndex());
    click(700, 500);
    EXPECT_EQ(-1, view->currentIndex());
    EXPECT_TRUE(manager.popupGrabs().empty());
}

TEST_F(MenuBarTest, TheDesktopShowsItsMainMenu)
{
    const std::shared_ptr<FakeMainMenu> menu = desktopMenus();
    const auto desktop = showDesktop(menu);
    ASSERT_EQ(titles(), (std::vector<std::string> {"", "Desktop", "File", "Edit"}));
    // The application's name is bold, its menus regular; File's plate starts
    // where its entry does, plus the inset.
    const double file = entryRect(2).x;
    EXPECT_EQ(entryRect(1).x + entryRect(1).width, file);

    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    // The desktop hears that File is about to open, once.
    EXPECT_EQ(1, menu->prepared(1));
    const auto bounds = menuBounds();
    ASSERT_TRUE(bounds);
    EXPECT_EQ(int(std::lround(file + 3)), bounds->x());
    EXPECT_EQ(38, bounds->y());

    // Moving onto Edit while File is open switches to Edit's menu; the
    // grab stays with the bar.
    move(entryCentre(3).x(), 16);
    frame();
    ASSERT_EQ(3, view->currentIndex());
    EXPECT_FALSE(view->entry(2)->isChecked());
    EXPECT_TRUE(view->entry(3)->isChecked());
    EXPECT_EQ(int(std::lround(entryRect(3).x + 3)), menuBounds()->x());
    EXPECT_EQ(1u, manager.popupGrabs().size());
    EXPECT_EQ(1, menu->closedCount(1));
    EXPECT_EQ(1, menu->prepared(2));

    // Right wraps around to the user's entry, Left comes back.
    key(KEY_RIGHT);
    EXPECT_EQ(0, view->currentIndex());
    key(KEY_LEFT);
    EXPECT_EQ(3, view->currentIndex());
    key(KEY_LEFT);
    EXPECT_EQ(2, view->currentIndex());
    EXPECT_EQ(1u, manager.popupGrabs().size());

    // Down lights New Window, Return chooses it: the desktop hears the
    // choice and the menu goes.
    key(KEY_DOWN);
    key(KEY_ENTER);
    EXPECT_EQ(-1, view->currentIndex());
    EXPECT_TRUE(manager.popupGrabs().empty());
    EXPECT_EQ(std::vector<int32_t> {10}, menu->activated);

    // Without a menu open, moving along the bar opens nothing.
    move(entryCentre(2).x(), 16);
    move(entryCentre(3).x(), 16);
    EXPECT_EQ(-1, view->currentIndex());
}

TEST_F(MenuBarTest, TheDesktopsProcessOtherWindowsDoNotShowItsMenus)
{
    // The file manager's daemon draws the desktop and owns the browser
    // windows: a focused browser window shows no File/Edit of the desktop's.
    const auto desktop = showDesktop(desktopMenus());
    ASSERT_EQ(titles(), (std::vector<std::string> {"", "Desktop", "File", "Edit"}));

    shell::TaskWindow browser;
    browser.appId = "cutefish-filemanager";
    browser.title = "Home";
    browser.pid = getpid();
    const uint64_t id = windows->add(browser);
    windows->setActive(id);
    ASSERT_TRUE(test::runUntil([&] { return !view->model().isDesktop(); }));
    test::runFor(std::chrono::milliseconds(100));
    frame();
    const std::vector<std::string> shown = titles();
    EXPECT_EQ(2u, shown.size());
    EXPECT_EQ(shown.end(), std::find(shown.begin(), shown.end(), "File"));

    // Back on the desktop, its menus return.
    windows->setActive(0);
    ASSERT_TRUE(test::runUntil([&] { return view->model().isDesktop() && view->model().count() == 4; }));
    EXPECT_EQ(titles(), (std::vector<std::string> {"", "Desktop", "File", "Edit"}));
}

TEST_F(MenuBarTest, AnApplicationShowsItsNameItsMenusAndTheirChanges)
{
    test::DBusTestClient application;
    FakeMenuExporter exporter(application.connection.get(), "/MenuBar");
    exporter.layouts[0] = layout(0, {},
                                 {layout(1, {{"label", label("_File")}, {"children-display", submenu()}}),
                                  layout(2, {{"label", label("_View")}, {"children-display", submenu()}}),
                                  layout(3, {{"label", label("Hidden")}, {"visible", dbus::Variant(false)}}),
                                  layout(4, {{"label", label("_Run")}})});
    exporter.layouts[1] = layout(1, {}, {layout(10, {{"label", label("_Open")}})});
    exporter.layouts[2] = layout(2, {}, {layout(20, {{"label", label("Zoom")}})});

    shell::TaskWindow terminal;
    terminal.appId = "terminal";
    terminal.title = "Terminal window";
    terminal.pid = 4242;
    terminal.menuService = application.connection->uniqueName();
    terminal.menuPath = "/MenuBar";
    const uint64_t id = windows->add(terminal);
    windows->setActive(id);
    ASSERT_TRUE(test::runUntil([&] { return view->model().count() == 5; }));
    frame();
    EXPECT_FALSE(view->model().isDesktop());
    // The entry's name; mnemonic markers gone; hidden menus have no entry.
    EXPECT_EQ(titles(), (std::vector<std::string> {"", "Terminal", "File", "View", "Run"}));

    // The application entry: Hide Terminal, Hide Others, Quit.
    click(entryCentre(1).x(), 16);
    ASSERT_EQ(1, view->currentIndex());
    key(KEY_DOWN);
    key(KEY_ENTER);
    ASSERT_FALSE(windows->requests.empty());
    EXPECT_EQ("hideApplication " + std::to_string(id), windows->requests.back());
    click(entryCentre(1).x(), 16);
    key(KEY_END);
    key(KEY_ENTER);
    EXPECT_EQ("quit " + std::to_string(id), windows->requests.back());

    // A plain action entry is triggered, not opened.
    click(entryCentre(4).x(), 16);
    EXPECT_EQ(-1, view->currentIndex());
    EXPECT_TRUE(test::runUntil([&] { return exporter.called("Event 4 clicked"); }));

    // The application asks for View (its Alt+V): it opens from the keyboard.
    exporter.object.emitSignal("ItemActivationRequested", int32_t(2), uint32_t(0));
    ASSERT_TRUE(test::runUntil([&] { return view->currentIndex() == 3; }));
    frame();
    ASSERT_TRUE(menuBounds());

    // A renamed menu renames its entry; the open one stays open.
    exporter.object.emitSignal("ItemsPropertiesUpdated",
                               std::vector<std::tuple<int32_t, dbus::VariantMap>> {
                                       {4, {{"label", dbus::Variant(std::string("_Go"))}}}},
                               std::vector<std::tuple<int32_t, std::vector<std::string>>> {});
    ASSERT_TRUE(test::runUntil([&] { return titles().back() == "Go"; }));
    EXPECT_EQ(3, view->currentIndex());
    EXPECT_TRUE(view->openMenu() && view->openMenu()->isOpen());

    // Another application takes the bar: the open menu goes.
    windows->setActive(0);
    ASSERT_TRUE(test::runUntil([&] { return view->model().isDesktop(); }));
    EXPECT_EQ(-1, view->currentIndex());
    EXPECT_TRUE(manager.popupGrabs().empty());
    EXPECT_EQ(titles(), (std::vector<std::string> {"", "Desktop"}));

    // The user's menu stays open across a switch.
    click(entryCentre(0).x(), 16);
    ASSERT_EQ(0, view->currentIndex());
    windows->setActive(id);
    ASSERT_TRUE(test::runUntil([&] { return view->model().count() == 5; }));
    EXPECT_EQ(0, view->currentIndex());
    EXPECT_TRUE(view->openMenu()->isOpen());
}

TEST_F(MenuBarTest, AMainMenuShowsItsApplicationMenuUnderTheName)
{
    // Archiver's main menu: its application menu (About, Quit) and File.
    auto menu = std::make_shared<FakeMainMenu>();
    menu->add(0, 1, "Archiver", compositor::MenuItem::Role::Application);
    menu->add(1, 10, "About Archiver");
    menu->add(1, 11, "Quit Archiver");
    menu->add(0, 2, "_File", compositor::MenuItem::Role::None);
    menu->add(2, 20, "Open");

    shell::TaskWindow archiver;
    archiver.appId = "archiver";
    archiver.title = "Archiver (ZIP)";
    archiver.pid = 4343;
    archiver.mainMenu = menu;
    const uint64_t id = windows->add(archiver);
    windows->setActive(id);
    ASSERT_TRUE(test::runUntil([&] { return view->model().count() == 3; }));
    frame();
    // Ready at once; the application menu has no entry of its own, and
    // without a desktop entry the name is its title, not the window's.
    EXPECT_EQ(titles(), (std::vector<std::string> {"", "Archiver", "File"}));

    // The name opens it, in place of the bar's Hide and Quit; the
    // application hears that it is about to open.
    click(entryCentre(1).x(), 16);
    ASSERT_EQ(1, view->currentIndex());
    frame();
    EXPECT_EQ(1, menu->prepared(1));
    key(KEY_DOWN);
    key(KEY_ENTER);
    EXPECT_EQ(std::vector<int32_t> {10}, menu->activated);
    EXPECT_TRUE(windows->requests.empty());
    EXPECT_EQ(1, menu->closedCount(1));

    // An entry the application renames while its menu is open changes there.
    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    frame();
    menu->rename(20, "Open…");
    test::runFor(std::chrono::milliseconds(20));
    frame();
    EXPECT_TRUE(view->openMenu() && view->openMenu()->isOpen());
    key(KEY_DOWN);
    key(KEY_ENTER);
    EXPECT_EQ((std::vector<int32_t> {10, 20}), menu->activated);
}

TEST_F(MenuBarTest, AnApplicationThatLeavesTakesItsOpenMenu)
{
    auto application = std::make_unique<test::DBusTestClient>();
    auto exporter = std::make_unique<FakeMenuExporter>(application->connection.get(), "/MenuBar");
    exporter->layouts[0] = layout(0, {}, {layout(1, {{"label", label("_File")}, {"children-display", submenu()}})});
    exporter->layouts[1] = layout(1, {}, {layout(10, {{"label", label("_Open")}})});

    shell::TaskWindow terminal;
    terminal.appId = "terminal";
    terminal.title = "Terminal window";
    terminal.pid = 4242;
    terminal.menuService = application->connection->uniqueName();
    terminal.menuPath = "/MenuBar";
    windows->setActive(windows->add(terminal));
    ASSERT_TRUE(test::runUntil([&] { return view->model().count() == 3; }));
    frame();
    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    ASSERT_TRUE(view->openMenu() && view->openMenu()->isOpen());

    // Its exporter leaves the bus with the menu up: the menu closes, and
    // nothing reaches the menu client that went with the exporter.
    exporter.reset();
    application.reset();
    ASSERT_TRUE(test::runUntil([&] { return view->currentIndex() == -1; }));
    frame();
    EXPECT_TRUE(manager.popupGrabs().empty());
}

TEST_F(MenuBarTest, AnOpenMenuStaysOpenWhenTheApplicationRenumbersItsMenus)
{
    test::DBusTestClient application;
    FakeMenuExporter exporter(application.connection.get(), "/MenuBar");
    exporter.layouts[0] = layout(0, {},
                                 {layout(1, {{"label", label("_File")}, {"children-display", submenu()}}),
                                  layout(2, {{"label", label("_Edit")}, {"children-display", submenu()}})});
    exporter.layouts[1] = layout(1, {}, {layout(10, {{"label", label("_Open")}})});
    exporter.layouts[2] = layout(2, {}, {layout(20, {{"label", label("_Undo")}})});
    showTerminal(application, 4);
    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    ASSERT_TRUE(test::runUntil([&] { return exporter.called("Event 1 opened"); }));

    // The application lays out its whole bar again under new ids while File
    // is open: File stays open where it was, shows the new File's entries
    // once they are in, and is not opened again.
    exporter.layouts.clear();
    exporter.layouts[0] = layout(0, {},
                                 {layout(101, {{"label", label("_File")}, {"children-display", submenu()}}),
                                  layout(102, {{"label", label("_Edit")}, {"children-display", submenu()}})});
    exporter.layouts[101] = layout(101, {}, {layout(110, {{"label", label("_Open")}})});
    exporter.layouts[102] = layout(102, {}, {layout(120, {{"label", label("_Undo")}})});
    exporter.layoutUpdated();
    ASSERT_TRUE(test::runUntil([&] { return view->model().rows()[2].id == 101; }));
    ASSERT_TRUE(test::runUntil([&] { return exporter.called("GetLayout 101"); }));
    test::runFor(std::chrono::milliseconds(50));
    frame();
    EXPECT_EQ(2, view->currentIndex());
    ASSERT_TRUE(view->openMenu() && view->openMenu()->isOpen());
    EXPECT_EQ(int(std::lround(entryRect(2).x + 3)), menuBounds()->x());
    EXPECT_EQ(1u, manager.popupGrabs().size());
    EXPECT_EQ(1, exporter.openedEvents());

    // Its rows are the new File's, and closing reaches the new File.
    chooseFirstRow();
    EXPECT_EQ(-1, view->currentIndex());
    ASSERT_TRUE(test::runUntil([&] { return exporter.called("Event 101 closed"); }));
    EXPECT_TRUE(exporter.called("Event 110 clicked"));
    EXPECT_FALSE(exporter.called("Event 1 closed"));
    EXPECT_EQ(1, exporter.openedEvents());

    // Moving on still switches menus.
    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    move(entryCentre(3).x(), 16);
    frame();
    EXPECT_EQ(3, view->currentIndex());
    EXPECT_TRUE(view->openMenu() && view->openMenu()->isOpen());
    EXPECT_TRUE(test::runUntil([&] { return exporter.called("Event 102 opened"); }));
}

TEST_F(MenuBarTest, OnlyAClickChoosesAPlainActionEntry)
{
    test::DBusTestClient application;
    FakeMenuExporter exporter(application.connection.get(), "/MenuBar");
    exporter.layouts[0] = layout(0, {},
                                 {layout(1, {{"label", label("_File")}, {"children-display", submenu()}}),
                                  layout(4, {{"label", label("_Run")}}),
                                  layout(2, {{"label", label("_Edit")}, {"children-display", submenu()}})});
    exporter.layouts[1] = layout(1, {}, {layout(10, {{"label", label("_Open")}})});
    exporter.layouts[2] = layout(2, {}, {layout(20, {{"label", label("_Undo")}})});
    showTerminal(application, 5);
    ASSERT_EQ(titles(), (std::vector<std::string> {"", "Terminal", "File", "Run", "Edit"}));

    // With File open, moving onto Run leaves File open.
    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    move(entryCentre(3).x(), 16);
    frame();
    EXPECT_EQ(2, view->currentIndex());
    EXPECT_TRUE(view->openMenu() && view->openMenu()->isOpen());

    // Right and Left pass over it.
    key(KEY_RIGHT);
    EXPECT_EQ(4, view->currentIndex());
    key(KEY_LEFT);
    EXPECT_EQ(2, view->currentIndex());

    // The application asking for it opens nothing either.
    key(KEY_ESC);
    ASSERT_EQ(-1, view->currentIndex());
    exporter.object.emitSignal("ItemActivationRequested", int32_t(4), uint32_t(0));
    test::runFor(std::chrono::milliseconds(50));
    frame();
    EXPECT_EQ(-1, view->currentIndex());
    EXPECT_FALSE(exporter.called("Event 4 clicked"));

    // A click chooses it, and closes the open menu.
    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    click(entryCentre(3).x(), 16);
    EXPECT_EQ(-1, view->currentIndex());
    EXPECT_TRUE(manager.popupGrabs().empty());
    EXPECT_TRUE(test::runUntil([&] { return exporter.called("Event 4 clicked"); }));
    EXPECT_EQ(1, exporter.count("Event 4 clicked"));
}

TEST_F(MenuBarTest, AMenusLayoutUpdateAfterItShowedIsNotLost)
{
    test::DBusTestClient application;
    FakeMenuExporter exporter(application.connection.get(), "/MenuBar");
    exporter.layouts[0] = layout(0, {}, {layout(1, {{"label", label("_File")}, {"children-display", submenu()}})});
    exporter.layouts[1] = layout(1, {}, {layout(10, {{"label", label("_Open")}})});
    showTerminal(application, 3);
    // Readied for the bar: asked AboutToShow and fetched.
    ASSERT_TRUE(test::runUntil([&] { return exporter.count("GetLayout 1") == 1; }));

    // The application changes File and says so: the next show has it.
    exporter.layouts[1] = layout(1, {}, {layout(11, {{"label", label("_Save")}})});
    exporter.layoutUpdated(1);
    ASSERT_TRUE(test::runUntil([&] { return exporter.count("GetLayout 1") == 2; }));
    test::runFor(std::chrono::milliseconds(50));
    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    test::runFor(std::chrono::milliseconds(50));
    frame();
    chooseFirstRow();
    EXPECT_TRUE(test::runUntil([&] { return exporter.called("Event 11 clicked"); }));
    EXPECT_FALSE(exporter.called("Event 10 clicked"));
}

TEST_F(MenuBarTest, ARootLayoutUpdateRefreshesLoadedMenus)
{
    test::DBusTestClient application;
    FakeMenuExporter exporter(application.connection.get(), "/MenuBar");
    exporter.layouts[0] = layout(0, {}, {layout(1, {{"label", label("_File")}, {"children-display", submenu()}})});
    exporter.layouts[1] = layout(1, {}, {layout(10, {{"label", label("_Open")}})});
    showTerminal(application, 3);
    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    test::runFor(std::chrono::milliseconds(50));
    const int fetched = exporter.count("GetLayout 1");

    // A layout update of the root, as some exporters send for any change,
    // with every id kept: the open File is fetched again and changes.
    exporter.layouts[1] = layout(1, {}, {layout(11, {{"label", label("_Save")}})});
    exporter.layoutUpdated();
    ASSERT_TRUE(test::runUntil([&] { return exporter.count("GetLayout 1") > fetched; }));
    test::runFor(std::chrono::milliseconds(50));
    frame();
    ASSERT_EQ(2, view->currentIndex());
    chooseFirstRow();
    EXPECT_TRUE(test::runUntil([&] { return exporter.called("Event 11 clicked"); }));

    // Closed, File changes again: it is current when it next shows.
    exporter.layouts[1] = layout(1, {}, {layout(12, {{"label", label("_Close")}})});
    const int roots = exporter.count("GetLayout 0");
    exporter.layoutUpdated();
    ASSERT_TRUE(test::runUntil([&] { return exporter.count("GetLayout 0") > roots; }));
    test::runFor(std::chrono::milliseconds(50));
    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    test::runFor(std::chrono::milliseconds(50));
    frame();
    chooseFirstRow();
    EXPECT_TRUE(test::runUntil([&] { return exporter.called("Event 12 clicked"); }));
}

TEST_F(MenuBarTest, AnOpenMenuStaysOpenWhenAMainMenuIsBuiltAgain)
{
    auto menu = std::make_shared<FakeMainMenu>();
    menu->add(1, 10, "Open");
    menu->add(0, 1, "File");
    menu->add(2, 20, "Undo");
    menu->add(0, 2, "Edit");

    shell::TaskWindow archiver;
    archiver.appId = "archiver";
    archiver.title = "Archiver";
    archiver.pid = 4343;
    archiver.mainMenu = menu;
    windows->setActive(windows->add(archiver));
    ASSERT_TRUE(test::runUntil([&] { return view->model().count() == 4; }));
    frame();
    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    ASSERT_EQ(1, menu->prepared(1));

    // The client takes every entry of its bar away and makes them again,
    // one request at a time: the bar changes once, and File stays open on
    // the new File without the client hearing of another opening.
    int changes = 0;
    auto subscription = view->model().rowsChanged.connect([&] { ++changes; });
    menu->remove(1);
    menu->remove(2);
    menu->add(3, 30, "Close");
    menu->insert(0, 3, "File");
    menu->add(4, 40, "Redo");
    menu->insert(0, 4, "Edit");
    test::runFor(std::chrono::milliseconds(20));
    frame();
    EXPECT_EQ(1, changes);
    EXPECT_EQ(titles(), (std::vector<std::string> {"", "Archiver", "File", "Edit"}));
    EXPECT_EQ(3, view->model().rows()[2].id);
    EXPECT_EQ(2, view->currentIndex());
    ASSERT_TRUE(view->openMenu() && view->openMenu()->isOpen());
    EXPECT_EQ(0, menu->prepared(3));
    EXPECT_EQ(0, menu->closedCount(1));

    chooseFirstRow();
    EXPECT_EQ(std::vector<int32_t> {30}, menu->activated);
    EXPECT_EQ(1, menu->closedCount(3));
    EXPECT_EQ(0, menu->closedCount(1));
}

TEST_F(MenuBarTest, AnOpenSubmenuStaysOpenWhenOpeningItRenumbersTheMenus)
{
    // View > Toolbars, every id in it offset by `base`.
    const auto lay = [](FakeMenuExporter &exporter, int32_t base) {
        exporter.layouts.clear();
        exporter.layouts[0] =
                layout(0, {}, {layout(base + 1, {{"label", label("_View")}, {"children-display", submenu()}})});
        exporter.layouts[base + 1] =
                layout(base + 1, {},
                       {layout(base + 10, {{"label", label("_Toolbars")}, {"children-display", submenu()}}),
                        layout(base + 11, {{"label", label("_Zoom")}})});
        exporter.layouts[base + 10] = layout(base + 10, {},
                                             {layout(base + 100, {{"label", label("_Menu Bar")}}),
                                              layout(base + 101, {{"label", label("_Bookmarks")}})});
    };
    test::DBusTestClient application;
    FakeMenuExporter exporter(application.connection.get(), "/MenuBar");
    lay(exporter, 0);
    // An application that lays out its whole menu again under new ids each
    // time Toolbars opens.
    int32_t base = 0;
    int toolbarsOpened = 0;
    exporter.onEvent = [&](int32_t id, const std::string &event) {
        if (event != "opened" || id != base + 10)
            return;
        ++toolbarsOpened;
        base += 1000;
        lay(exporter, base);
        exporter.layoutUpdated();
    };
    showTerminal(application, 3);

    click(entryCentre(2).x(), 16);
    ASSERT_EQ(2, view->currentIndex());
    ASSERT_TRUE(test::runUntil([&] { return exporter.called("GetLayout 10"); }));
    test::runFor(std::chrono::milliseconds(50));
    frame();
    key(KEY_DOWN);
    key(KEY_RIGHT);
    ASSERT_TRUE(test::runUntil([&] { return toolbarsOpened == 1; }));
    ASSERT_TRUE(test::runUntil([&] { return exporter.called("GetLayout 1010"); }));
    test::runFor(std::chrono::milliseconds(200));
    frame();

    // Both menus stay open, now on the new ids, and Toolbars was opened once.
    EXPECT_EQ(1, toolbarsOpened);
    EXPECT_EQ(1000, base);
    EXPECT_EQ(1001, view->model().rows()[2].id);
    ASSERT_TRUE(view->openMenu() && view->openMenu()->isOpen());
    ASSERT_TRUE(view->openMenu()->submenu(0) && view->openMenu()->submenu(0)->isOpen());
    EXPECT_EQ(2, exporter.openedEvents());

    // Its lit first row is the new Menu Bar; closing reaches the new ids.
    key(KEY_ENTER);
    EXPECT_EQ(-1, view->currentIndex());
    ASSERT_TRUE(test::runUntil([&] { return exporter.called("Event 1001 closed"); }));
    EXPECT_TRUE(test::runUntil([&] { return exporter.called("Event 1010 closed"); }));
    EXPECT_TRUE(exporter.called("Event 1100 clicked"));
    EXPECT_FALSE(exporter.called("Event 100 clicked"));
    EXPECT_FALSE(exporter.called("Event 1 closed"));
    EXPECT_FALSE(exporter.called("Event 10 closed"));
    EXPECT_EQ(1, toolbarsOpened);
}
