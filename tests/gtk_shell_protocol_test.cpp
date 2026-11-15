// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <map>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-server-core.h>
#include <gtk-shell-client-protocol.h>

#include "compositor/compositor.h"
#include "compositor/menu_model.h"
#include "compositor/surface.h"
#include "scene/node.h"
#include "scene/scene.h"
#include "wayland/protocols/gtk_shell.h"
#include "wayland/protocols/wl_compositor.h"
#include "wayland/server.h"

using namespace cutefish;

namespace {

struct Client {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_compositor *compositor = nullptr;
    gtk_shell1 *shell = nullptr;
    std::map<std::string, std::pair<uint32_t, uint32_t>> globals;
    uint32_t capabilities = 0;
    bool capabilitiesSent = false;

    explicit Client(int fd)
        : display(wl_display_connect_to_fd(fd))
    {
        registry = wl_display_get_registry(display);
        static const wl_registry_listener listener = {
                [](void *data, wl_registry *, uint32_t name, const char *interface, uint32_t version) {
                    static_cast<Client *>(data)->globals[interface] = {name, version};
                },
                [](void *, wl_registry *, uint32_t) {}};
        wl_registry_add_listener(registry, &listener, this);
        wl_display_roundtrip(display);
        compositor = static_cast<wl_compositor *>(
                wl_registry_bind(registry, globals["wl_compositor"].first, &wl_compositor_interface, 4));
        shell = static_cast<gtk_shell1 *>(
                wl_registry_bind(registry, globals["gtk_shell1"].first, &gtk_shell1_interface, 1));
        static const gtk_shell1_listener shellListener = {
                [](void *data, gtk_shell1 *, uint32_t capabilities) {
                    static_cast<Client *>(data)->capabilities = capabilities;
                    static_cast<Client *>(data)->capabilitiesSent = true;
                }};
        gtk_shell1_add_listener(shell, &shellListener, this);
        sync();
    }

    ~Client()
    {
        gtk_shell1_destroy(shell);
        wl_compositor_destroy(compositor);
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
    }

    void sync() { wl_display_roundtrip(display); }
};

class GtkShellProtocolTest : public testing::Test
{
protected:
    scene::Scene scene;
    scene::Node *surfaceContainer = scene.root()->addChild(std::make_unique<scene::Node>("surfaces"));
    compositor::Compositor compositor {surfaceContainer};
    wayland::Server server;

    std::mutex mutex;
    std::function<void()> pendingCheck;

    compositor::GtkMenuAddress address() const
    {
        for (compositor::Surface *surface : compositor.surfaces()) {
            if (!surface->gtkMenuAddress().busName.empty() || !surface->gtkMenuAddress().menubarPath.empty())
                return surface->gtkMenuAddress();
        }
        return {};
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
        ASSERT_TRUE(wayland::createCompositorGlobal(server.display(), &compositor));
        ASSERT_TRUE(wayland::createGtkShellGlobal(server.display()));

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

} // namespace

TEST_F(GtkShellProtocolTest, OffersVersionOneWithTheGlobalMenus)
{
    run([&](Client &client) {
        EXPECT_EQ(client.globals["gtk_shell1"].second, 1u);
        ASSERT_TRUE(client.capabilitiesSent);
        const uint32_t appMenu = 1u << (GTK_SHELL1_CAPABILITY_GLOBAL_APP_MENU - 1);
        const uint32_t menuBar = 1u << (GTK_SHELL1_CAPABILITY_GLOBAL_MENU_BAR - 1);
        EXPECT_EQ(client.capabilities, appMenu | menuBar);
    });
}

TEST_F(GtkShellProtocolTest, NamesTheSurfacesMenus)
{
    run([&](Client &client) {
        wl_surface *surface = wl_compositor_create_surface(client.compositor);
        gtk_surface1 *gtkSurface = gtk_shell1_get_gtk_surface(client.shell, surface);
        gtk_surface1_set_dbus_properties(gtkSurface, "org.example.Editor", "/org/example/Editor/menus/appmenu",
                                         "/org/example/Editor/menus/menubar", "/org/example/Editor/window/1",
                                         "/org/example/Editor", ":1.42");
        client.sync();
        check([&] {
            const compositor::GtkMenuAddress found = address();
            EXPECT_EQ(found.busName, ":1.42");
            EXPECT_EQ(found.menubarPath, "/org/example/Editor/menus/menubar");
            EXPECT_EQ(found.appMenuPath, "/org/example/Editor/menus/appmenu");
            EXPECT_EQ(found.windowPath, "/org/example/Editor/window/1");
            EXPECT_EQ(found.applicationPath, "/org/example/Editor");
            EXPECT_TRUE(found.hasMenus());
        });

        // A well-known name does not say which connection the actions are
        // on, and a malformed path is no path.
        gtk_surface1_set_dbus_properties(gtkSurface, nullptr, nullptr, "/org/example/menus/menu bar", nullptr,
                                         nullptr, "org.example.Editor");
        client.sync();
        check([&] {
            for (compositor::Surface *found : compositor.surfaces())
                EXPECT_FALSE(found->gtkMenuAddress().hasMenus());
        });

        // Version 1 has no destructor: the surface may go first.
        wl_surface_destroy(surface);
        client.sync();
        gtk_surface1_destroy(gtkSurface);
    });
}
