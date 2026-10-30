// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>
#include <wayland-server-core.h>
#include <cutefish-main-menu-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>

#include "compositor/compositor.h"
#include "compositor/menu_model.h"
#include "compositor/surface.h"
#include "input/seat.h"
#include "scene/node.h"
#include "scene/scene.h"
#include "wayland/protocols/main_menu.h"
#include "wayland/protocols/wl_compositor.h"
#include "wayland/protocols/wl_seat.h"
#include "wayland/protocols/xdg_shell.h"
#include "wayland/server.h"
#include "wm/window_manager.h"

using namespace cutefish;

namespace {

// What the client heard of one menu or item.
struct Menu {
    cutefish_menu_v1 *menu = nullptr;
    uint32_t count = 0;
    std::vector<uint32_t> willOpen;
    int closed = 0;
};

struct Item {
    cutefish_menu_item_v1 *item = nullptr;
    int activated = 0;
    uint32_t serial = 0;
};

const cutefish_menu_v1_listener menuListener = {
        [](void *data, cutefish_menu_v1 *, uint32_t serial) { static_cast<Menu *>(data)->willOpen.push_back(serial); },
        [](void *data, cutefish_menu_v1 *) { ++static_cast<Menu *>(data)->closed; },
};

const cutefish_menu_item_v1_listener itemListener = {
        [](void *data, cutefish_menu_item_v1 *, uint32_t serial) {
            ++static_cast<Item *>(data)->activated;
            static_cast<Item *>(data)->serial = serial;
        },
};

struct Client {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_compositor *compositor = nullptr;
    xdg_wm_base *wmBase = nullptr;
    cutefish_main_menu_manager_v1 *menus = nullptr;
    std::map<std::string, uint32_t> globals;
    std::vector<std::unique_ptr<Menu>> madeMenus;
    std::vector<std::unique_ptr<Item>> madeItems;

    wl_surface *surface = nullptr;
    xdg_surface *xdgSurface = nullptr;
    xdg_toplevel *toplevel = nullptr;

    explicit Client(int fd)
        : display(wl_display_connect_to_fd(fd))
    {
        registry = wl_display_get_registry(display);
        static const wl_registry_listener listener = {
                [](void *data, wl_registry *, uint32_t name, const char *interface, uint32_t) {
                    static_cast<Client *>(data)->globals[interface] = name;
                },
                [](void *, wl_registry *, uint32_t) {}};
        wl_registry_add_listener(registry, &listener, this);
        wl_display_roundtrip(display);
        compositor = static_cast<wl_compositor *>(
                wl_registry_bind(registry, globals["wl_compositor"], &wl_compositor_interface, 4));
        wmBase = static_cast<xdg_wm_base *>(
                wl_registry_bind(registry, globals["xdg_wm_base"], &xdg_wm_base_interface, 3));
        menus = static_cast<cutefish_main_menu_manager_v1 *>(wl_registry_bind(
                registry, globals["cutefish_main_menu_manager_v1"], &cutefish_main_menu_manager_v1_interface, 1));
        surface = wl_compositor_create_surface(compositor);
        xdgSurface = xdg_wm_base_get_xdg_surface(wmBase, surface);
        toplevel = xdg_surface_get_toplevel(xdgSurface);
        sync();
    }

    ~Client()
    {
        for (auto &item : madeItems) {
            if (item->item)
                cutefish_menu_item_v1_destroy(item->item);
        }
        for (auto &menu : madeMenus) {
            if (menu->menu)
                cutefish_menu_v1_destroy(menu->menu);
        }
        xdg_toplevel_destroy(toplevel);
        xdg_surface_destroy(xdgSurface);
        wl_surface_destroy(surface);
        cutefish_main_menu_manager_v1_destroy(menus);
        xdg_wm_base_destroy(wmBase);
        wl_compositor_destroy(compositor);
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
    }

    Menu *makeMenu()
    {
        madeMenus.push_back(std::make_unique<Menu>());
        Menu *menu = madeMenus.back().get();
        menu->menu = cutefish_main_menu_manager_v1_create_menu(menus);
        cutefish_menu_v1_add_listener(menu->menu, &menuListener, menu);
        return menu;
    }

    Item *makeItem(const char *title, uint32_t kind = CUTEFISH_MENU_ITEM_V1_KIND_NORMAL)
    {
        madeItems.push_back(std::make_unique<Item>());
        Item *item = madeItems.back().get();
        item->item = cutefish_main_menu_manager_v1_create_item(menus, kind);
        cutefish_menu_item_v1_add_listener(item->item, &itemListener, item);
        if (title)
            cutefish_menu_item_v1_set_title(item->item, title);
        return item;
    }

    // An item in `menu` at the end.
    Item *append(Menu *menu, const char *title)
    {
        Item *item = makeItem(title);
        cutefish_menu_v1_insert_item(menu->menu, item->item, menu->count++);
        return item;
    }

    void sync() { wl_display_roundtrip(display); }

    uint32_t syncExpectingError()
    {
        if (wl_display_roundtrip(display) >= 0 || wl_display_get_error(display) != EPROTO)
            return UINT32_MAX;
        const wl_interface *raisedOn = nullptr;
        uint32_t id = 0;
        return wl_display_get_protocol_error(display, &raisedOn, &id);
    }
};

class MainMenuProtocolTest : public testing::Test
{
protected:
    scene::Scene scene;
    scene::Node *surfaceContainer = scene.root()->addChild(std::make_unique<scene::Node>("surfaces"));
    compositor::Compositor compositor {surfaceContainer};
    input::Seat seat {&compositor};
    wm::WindowManager windowManager {&compositor, &seat};
    wayland::Server server;

    std::mutex mutex;
    std::function<void()> pendingCheck;

    // The window's main menu, as the status bar finds it.
    std::shared_ptr<compositor::MenuModel> mainMenu() const
    {
        for (compositor::Surface *surface : compositor.surfaces()) {
            if (surface->mainMenu())
                return surface->mainMenu();
        }
        return nullptr;
    }

    // The labels under `id`, as the model has them.
    std::vector<std::string> labels(compositor::MenuModel &model, int32_t id) const
    {
        std::vector<std::string> found;
        for (int32_t child : model.item(id)->children)
            found.push_back(model.item(child)->label);
        return found;
    }

    void check(std::function<void()> body)
    {
        std::promise<void> done;
        auto future = done.get_future();
        {
            std::lock_guard lock(mutex);
            pendingCheck = [&] {
                body();
                done.set_value();
            };
        }
        future.wait();
    }

    void run(std::function<void(Client &)> body)
    {
        auto xdgShell = std::make_unique<wayland::WaylandXdgShell>(&compositor, &windowManager, &seat,
                                                                   server.serialTracker());
        ASSERT_TRUE(wayland::createCompositorGlobal(server.display(), &compositor));
        ASSERT_TRUE(wayland::createXdgShellGlobal(server.display(), xdgShell.get()));
        wayland::WaylandSeat waylandSeat(&seat, server.serialTracker());
        ASSERT_TRUE(wayland::createMainMenuGlobal(server.display(), &waylandSeat));

        int fds[2];
        ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds));
        ASSERT_TRUE(wl_client_create(server.display(), fds[0]));

        std::atomic<bool> done = false;
        std::thread client([&] {
            {
                Client c(fds[1]);
                body(c);
            }
            done = true;
        });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done) {
            server.dispatch(std::chrono::milliseconds(1));
            server.flush();
            std::function<void()> work;
            {
                std::lock_guard lock(mutex);
                work.swap(pendingCheck);
            }
            if (work)
                work();
            if (std::chrono::steady_clock::now() > deadline) {
                ADD_FAILURE() << "client timed out";
                shutdown(fds[1], SHUT_RDWR);
                break;
            }
        }
        client.join();
        wl_display_destroy_clients(server.display());
    }
};

TEST_F(MainMenuProtocolTest, AWindowsMainMenuIsItsSurfacesModel)
{
    run([&](Client &c) {
        Menu *main = c.makeMenu();
        Item *application = c.append(main, "Archiver");
        Item *file = c.append(main, "File_Edit");
        Menu *applicationMenu = c.makeMenu();
        cutefish_menu_v1_set_role(applicationMenu->menu, CUTEFISH_MENU_V1_ROLE_APPLICATION);
        c.append(applicationMenu, "About");
        Item *quit = c.append(applicationMenu, "Quit");
        cutefish_menu_item_v1_set_key_equivalent(quit->item, "q",
            CUTEFISH_MENU_ITEM_V1_MODIFIER_CONTROL | CUTEFISH_MENU_ITEM_V1_MODIFIER_SHIFT);
        cutefish_menu_item_v1_set_submenu(application->item, applicationMenu->menu);
        Menu *fileMenu = c.makeMenu();
        Item *open = c.append(fileMenu, "Open");
        cutefish_menu_item_v1_set_enabled(open->item, 0);
        cutefish_menu_item_v1_set_state(open->item, CUTEFISH_MENU_ITEM_V1_STATE_ON);
        cutefish_menu_item_v1_set_submenu(file->item, fileMenu->menu);
        cutefish_main_menu_manager_v1_set_main_menu(c.menus, c.surface, main->menu);
        c.sync();

        int32_t applicationId = 0;
        int32_t quitId = 0;
        check([&] {
            const std::shared_ptr<compositor::MenuModel> model = mainMenu();
            ASSERT_TRUE(model);
            // Underscores stay underscores: the model marks mnemonics.
            EXPECT_EQ((std::vector<std::string> {"Archiver", "File__Edit"}), labels(*model, 0));
            applicationId = model->root().children[0];
            const compositor::MenuItem *item = model->item(applicationId);
            EXPECT_TRUE(item->hasSubmenu);
            EXPECT_EQ(compositor::MenuItem::Role::Application, item->role);
            EXPECT_EQ((std::vector<std::string> {"About", "Quit"}), labels(*model, applicationId));
            quitId = item->children[1];
            EXPECT_EQ(applicationId, model->item(quitId)->parent);
            EXPECT_EQ((std::vector<std::vector<std::string>> {{"Control", "Shift", "q"}}), model->item(quitId)->shortcut);
            const compositor::MenuItem *opened = model->item(model->item(model->root().children[1])->children[0]);
            EXPECT_FALSE(opened->enabled);
            EXPECT_EQ(1, opened->toggleState);

            // Before it opens the application hears will_open; a choice
            // reaches the item.
            model->prepare(applicationId);
            model->activate(quitId);
            model->closed(applicationId);
        });
        c.sync();
        ASSERT_EQ(1u, applicationMenu->willOpen.size());
        EXPECT_EQ(1, quit->activated);
        EXPECT_NE(0u, quit->serial);
        EXPECT_EQ(1, applicationMenu->closed);

        // The answer and later changes reach the model.
        std::vector<int32_t> updated;
        std::vector<int32_t> changed;
        std::vector<base::Subscription> subscriptions;
        check([&] {
            const std::shared_ptr<compositor::MenuModel> model = mainMenu();
            subscriptions.push_back(model->menuUpdated.connect([&](int32_t id) { updated.push_back(id); }));
            subscriptions.push_back(model->itemChanged.connect([&](int32_t id) { changed.push_back(id); }));
        });
        cutefish_menu_item_v1_set_title(quit->item, "Quit Archiver");
        c.append(applicationMenu, "Hide");
        cutefish_menu_v1_ready(applicationMenu->menu, applicationMenu->willOpen.back());
        c.sync();
        check([&] {
            EXPECT_EQ(std::vector<int32_t> {quitId}, changed);
            EXPECT_EQ((std::vector<int32_t> {applicationId, applicationId}), updated);
            EXPECT_EQ((std::vector<std::string> {"About", "Quit Archiver", "Hide"}), labels(*mainMenu(), applicationId));
            subscriptions.clear();
        });

        // Destroying the menu takes it from the window.
        cutefish_menu_v1_destroy(main->menu);
        main->menu = nullptr;
        c.sync();
        check([&] { EXPECT_FALSE(mainMenu()); });
    });
}

TEST_F(MainMenuProtocolTest, AnItemIsInOneMenuAtATime)
{
    run([&](Client &c) {
        Menu *first = c.makeMenu();
        Menu *second = c.makeMenu();
        Item *item = c.append(first, "Item");
        cutefish_menu_v1_insert_item(second->menu, item->item, 0);
        EXPECT_EQ(uint32_t(CUTEFISH_MENU_V1_ERROR_ITEM_IN_USE), c.syncExpectingError());
    });
}

TEST_F(MainMenuProtocolTest, AMenuCanNotOpenFromAnItemInsideIt)
{
    run([&](Client &c) {
        Menu *menu = c.makeMenu();
        Item *item = c.append(menu, "Item");
        cutefish_menu_item_v1_set_submenu(item->item, menu->menu);
        EXPECT_EQ(uint32_t(CUTEFISH_MENU_ITEM_V1_ERROR_SUBMENU_IN_USE), c.syncExpectingError());
    });
}

TEST_F(MainMenuProtocolTest, OnlyAToplevelHasAMainMenu)
{
    run([&](Client &c) {
        wl_surface *plain = wl_compositor_create_surface(c.compositor);
        Menu *menu = c.makeMenu();
        cutefish_main_menu_manager_v1_set_main_menu(c.menus, plain, menu->menu);
        EXPECT_EQ(uint32_t(CUTEFISH_MAIN_MENU_MANAGER_V1_ERROR_INVALID_SURFACE), c.syncExpectingError());
        wl_surface_destroy(plain);
    });
}

TEST_F(MainMenuProtocolTest, AnUnansweredSerialIsAnError)
{
    run([&](Client &c) {
        Menu *menu = c.makeMenu();
        cutefish_menu_v1_ready(menu->menu, 42);
        EXPECT_EQ(uint32_t(CUTEFISH_MENU_V1_ERROR_INVALID_SERIAL), c.syncExpectingError());
    });
}

} // namespace
