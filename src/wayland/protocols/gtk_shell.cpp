// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wayland/protocols/gtk_shell.h"

#include <algorithm>
#include <gtk-shell-server-protocol.h>
#include <new>
#include <string>
#include <string_view>

#include "compositor/menu_model.h"
#include "compositor/surface.h"
#include "compositor/surface_observer.h"
#include "wayland/protocols/dbus_address.h"
#include "wayland/server_util.h"

namespace cutefish::wayland {
namespace {

// Version 2 and later change how GTK tiles, focuses and moves its windows.
constexpr uint32_t kGtkShellVersion = 1;

// GTK hides its own menu bar and application menu while the desktop shows
// them.
constexpr uint32_t kCapabilities = (1u << (GTK_SHELL1_CAPABILITY_GLOBAL_APP_MENU - 1))
        | (1u << (GTK_SHELL1_CAPABILITY_GLOBAL_MENU_BAR - 1));

class GtkSurface final : public compositor::SurfaceObserver
{
public:
    explicit GtkSurface(compositor::Surface* surface)
        : m_surface(surface)
    {
        surface->addSurfaceObserver(this);
    }

    ~GtkSurface() override
    {
        if (m_surface) {
            m_surface->removeSurfaceObserver(this);
            m_surface->setGtkMenuAddress({});
        }
    }

    void onSurfaceDestroying(compositor::Surface* surface) override
    {
        surface->removeSurfaceObserver(this);
        m_surface = nullptr;
    }

    void setDBusProperties(const char* menubarPath, const char* appMenuPath, const char* windowPath,
                           const char* applicationPath, const char* busName)
    {
        if (!m_surface)
            return;
        const auto path = [](const char* text) {
            return text && validObjectPath(text) ? std::string(text) : std::string();
        };
        compositor::GtkMenuAddress address;
        // Only a unique name names the connection the actions are on.
        if (busName && std::string_view(busName).starts_with(':') && validBusName(busName))
            address.busName = busName;
        address.menubarPath = path(menubarPath);
        address.appMenuPath = path(appMenuPath);
        address.windowPath = path(windowPath);
        address.applicationPath = path(applicationPath);
        m_surface->setGtkMenuAddress(address);
    }

private:
    compositor::Surface* m_surface;
};

void setDBusProperties(wl_client*, wl_resource* resource, const char*, const char* appMenuPath,
                       const char* menubarPath, const char* windowPath, const char* applicationPath,
                       const char* busName)
{
    getUserDataAs<GtkSurface>(resource)->setDBusProperties(menubarPath, appMenuPath, windowPath,
                                                           applicationPath, busName);
}

// GTK does without the rest, as it does on a compositor that offers no gtk_shell1.
void setModal(wl_client*, wl_resource*) {}
void unsetModal(wl_client*, wl_resource*) {}
void present(wl_client*, wl_resource*, uint32_t) {}

const struct gtk_surface1_interface gtkSurfaceImplementation = {setDBusProperties, setModal, unsetModal,
                                                                present};

void getGtkSurface(wl_client* client, wl_resource* shell, uint32_t id, wl_resource* resource)
{
    auto* surface = surfaceFromResource(resource);
    if (!surface) {
        wl_client_post_implementation_error(client, "gtk_surface1 requires a live wl_surface");
        return;
    }
    auto* extension = wl_resource_create(client, &gtk_surface1_interface, wl_resource_get_version(shell), id);
    if (!extension) {
        wl_client_post_no_memory(client);
        return;
    }
    try {
        setImplementation(extension, &gtkSurfaceImplementation, std::make_unique<GtkSurface>(surface));
    } catch (const std::bad_alloc&) {
        wl_resource_destroy(extension);
        wl_client_post_no_memory(client);
    }
}

void setStartupId(wl_client*, wl_resource*, const char*) {}
void systemBell(wl_client*, wl_resource*, wl_resource*) {}

const struct gtk_shell1_interface gtkShellImplementation = {getGtkSurface, setStartupId, systemBell};

void bindGtkShell(wl_client* client, void*, uint32_t version, uint32_t id)
{
    auto* resource = wl_resource_create(client, &gtk_shell1_interface, std::min(version, kGtkShellVersion), id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &gtkShellImplementation, nullptr, nullptr);
    gtk_shell1_send_capabilities(resource, kCapabilities);
}

} // namespace

wl_global* createGtkShellGlobal(wl_display* display)
{
    return wl_global_create(display, &gtk_shell1_interface, kGtkShellVersion, nullptr, bindGtkShell);
}

} // namespace cutefish::wayland
