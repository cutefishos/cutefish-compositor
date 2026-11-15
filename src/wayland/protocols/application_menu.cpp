// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wayland/protocols/application_menu.h"

#include <algorithm>
#include <appmenu-server-protocol.h>
#include <new>
#include <string>
#include <string_view>

#include "compositor/surface.h"
#include "compositor/surface_observer.h"
#include "wayland/protocols/dbus_address.h"
#include "wayland/server_util.h"

namespace cutefish::wayland {
namespace {

constexpr uint32_t kAppMenuVersion = 2;

class MenuSurface final : public compositor::SurfaceObserver
{
public:
    explicit MenuSurface(compositor::Surface* surface)
        : m_surface(surface), m_source(surface->addMenuSource())
    {
        surface->addSurfaceObserver(this);
    }

    ~MenuSurface() override
    {
        if (m_surface) {
            m_surface->removeSurfaceObserver(this);
            m_surface->removeMenuSource(m_source);
        }
    }

    void onSurfaceDestroying(compositor::Surface* surface) override
    {
        surface->removeSurfaceObserver(this);
        m_surface = nullptr;
    }

    void setAddress(const char* service, const char* path)
    {
        if (!m_surface)
            return;
        const std::string_view serviceName(service);
        const std::string_view objectPath(path);
        if (!validBusName(serviceName) || !validObjectPath(objectPath)) {
            m_surface->setMenuAddress(m_source, {});
            return;
        }
        m_surface->setMenuAddress(m_source,
                                  {std::string(serviceName), std::string(objectPath)});
    }

private:
    compositor::Surface* m_surface;
    uint64_t m_source;
};

void destroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }

void setAddress(wl_client*, wl_resource* resource, const char* service, const char* path)
{
    getUserDataAs<MenuSurface>(resource)->setAddress(service, path);
}

const struct org_kde_kwin_appmenu_interface appMenuImplementation = {setAddress, destroy};

void createAppMenu(wl_client* client, wl_resource* manager, uint32_t id, wl_resource* resource)
{
    auto* surface = surfaceFromResource(resource);
    if (!surface) {
        wl_client_post_implementation_error(client, "menu requires a live wl_surface");
        return;
    }
    auto* extension = wl_resource_create(client, &org_kde_kwin_appmenu_interface,
                                          wl_resource_get_version(manager), id);
    if (!extension) {
        wl_client_post_no_memory(client);
        return;
    }
    try {
        setImplementation(extension, &appMenuImplementation,
                          std::make_unique<MenuSurface>(surface));
    } catch (const std::bad_alloc&) {
        wl_resource_destroy(extension);
        wl_client_post_no_memory(client);
    }
}

const struct org_kde_kwin_appmenu_manager_interface appMenuManagerImplementation = {createAppMenu, destroy};

void bindAppMenu(wl_client* client, void*, uint32_t version, uint32_t id)
{
    auto* resource = wl_resource_create(client, &org_kde_kwin_appmenu_manager_interface,
                                         std::min(version, kAppMenuVersion), id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &appMenuManagerImplementation, nullptr, nullptr);
}

} // namespace

wl_global* createAppMenuGlobal(wl_display* display)
{
    return wl_global_create(display, &org_kde_kwin_appmenu_manager_interface, kAppMenuVersion,
                            nullptr, bindAppMenu);
}

} // namespace cutefish::wayland
