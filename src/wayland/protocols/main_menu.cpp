// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wayland/protocols/main_menu.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <cutefish-main-menu-v1-server-protocol.h>

#include "compositor/menu_model.h"
#include "compositor/surface.h"
#include "compositor/surface_observer.h"
#include "input/seat.h"
#include "wayland/protocols/wl_seat.h"
#include "wayland/protocols/xdg_shell.h"
#include "wayland/serial_tracker.h"
#include "wayland/server_util.h"

namespace cutefish::wayland {
namespace {

constexpr uint32_t kMainMenuVersion = 1;
// Past these a title is cut and a menu takes no more items.
constexpr size_t kMaxTitleBytes = 4096;
constexpr size_t kMaxMenuItems = 4096;

class MenuObject;
class RootModel;

// One cutefish_menu_item_v1. Its MenuItem is kept current, so a model hands
// out a pointer to it.
class ItemObject
{
public:
    ItemObject(wl_resource *resource, int32_t id, bool separator);
    ~ItemObject();

    wl_resource *resource() const { return m_resource; }
    compositor::MenuItem &data() { return m_data; }
    MenuObject *menu() const { return m_menu; }
    void setMenu(MenuObject *menu);
    MenuObject *submenu() const { return m_submenu; }
    void setSubmenu(MenuObject *submenu);
    // The children, the role and the parent, from the submenu and the menu.
    void updateStructure();
    // The roots above it heard that its properties changed.
    void changed();

private:
    wl_resource *m_resource;
    compositor::MenuItem m_data;
    MenuObject *m_menu = nullptr;
    MenuObject *m_submenu = nullptr;
};

// The seat whose input a choice in a menu counts as.
WaylandSeat *&menuSeat()
{
    static WaylandSeat *seat = nullptr;
    return seat;
}

// A serial for the choice, which the client may spend on the selection as
// on a key press; the menu took the focus, so its older serials are gone.
uint32_t choiceSerial(wl_client *client)
{
    WaylandSeat *seat = menuSeat();
    if (!seat)
        return 0;
    const uint32_t serial = seat->serialTracker->nextSerial(SerialTracker::OTHER_EVENT);
    seat->seat->selection()->recordSerial(client, serial);
    seat->seat->primarySelection()->recordSerial(client, serial);
    return serial;
}

std::unordered_map<int32_t, ItemObject *> &itemsById()
{
    static std::unordered_map<int32_t, ItemObject *> items;
    return items;
}

int32_t nextItemId()
{
    // 0 is every model's root.
    static int32_t next = 0;
    if (++next <= 0)
        next = 1;
    return next;
}

// One cutefish_menu_v1.
class MenuObject
{
public:
    explicit MenuObject(wl_resource *resource)
        : m_resource(resource)
    {
    }
    ~MenuObject();

    wl_resource *resource() const { return m_resource; }
    const std::vector<ItemObject *> &items() const { return m_items; }
    ItemObject *owner() const { return m_owner; }
    void setOwner(ItemObject *owner);
    compositor::MenuItem::Role role() const { return m_role; }

    void insert(ItemObject *item, size_t index);
    void remove(ItemObject *item);
    void setRole(compositor::MenuItem::Role role);
    // The menu is about to show; the client answers with ready.
    void willOpen();
    bool ready(uint32_t serial);
    void closed();

    // The model this menu is the root of, made on first use and kept while a
    // window shows it.
    std::shared_ptr<RootModel> rootModel();
    // Its own model and those of the menus above it: they hear the changes.
    std::vector<std::shared_ptr<RootModel>> roots();
    // The id the models know the menu by: its owner's, or 0 at the root.
    int32_t modelId() const;
    // Its items changed: the item it opens from, or the root, heard.
    void structureChanged();

private:
    wl_resource *m_resource;
    std::vector<ItemObject *> m_items;
    ItemObject *m_owner = nullptr;
    compositor::MenuItem::Role m_role = compositor::MenuItem::Role::None;
    std::weak_ptr<RootModel> m_root;
    std::set<uint32_t> m_pendingSerials;
};

// A main menu as the status bar sees it: the menu's items under the root,
// their submenus under them. Windows keep it while they show the menu; it
// clears them when the menu goes.
class RootModel final : public compositor::MenuModel, public compositor::SurfaceObserver
{
public:
    explicit RootModel(MenuObject *menu)
        : m_menu(menu)
    {
        updateRoot();
    }

    ~RootModel() override
    {
        for (compositor::Surface *surface : m_surfaces)
            surface->removeSurfaceObserver(this);
    }

    MenuObject *menu() const { return m_menu; }

    void updateRoot()
    {
        m_root.id = 0;
        m_root.hasSubmenu = true;
        m_root.childrenLoaded = true;
        m_root.children.clear();
        if (m_menu) {
            for (ItemObject *item : m_menu->items())
                m_root.children.push_back(item->data().id);
        }
    }

    // The menu is gone: windows showing it show none.
    void detach()
    {
        m_menu = nullptr;
        updateRoot();
        const std::vector<compositor::Surface *> surfaces = m_surfaces;
        for (compositor::Surface *surface : surfaces) {
            surface->removeSurfaceObserver(this);
            if (surface->mainMenu().get() == this)
                surface->setMainMenu(nullptr);
        }
        m_surfaces.clear();
    }

    void addSurface(compositor::Surface *surface)
    {
        if (std::find(m_surfaces.begin(), m_surfaces.end(), surface) != m_surfaces.end())
            return;
        m_surfaces.push_back(surface);
        surface->addSurfaceObserver(this);
    }

    void onSurfaceDestroying(compositor::Surface *surface) override
    {
        surface->removeSurfaceObserver(this);
        std::erase(m_surfaces, surface);
    }

    const compositor::MenuItem *item(int32_t id) const override
    {
        if (id == 0)
            return &m_root;
        const auto it = itemsById().find(id);
        return it == itemsById().end() ? nullptr : &it->second->data();
    }

    void refresh(int32_t id) override { prepare(id); }

    void prepare(int32_t id) override
    {
        if (MenuObject *menu = menuFor(id))
            menu->willOpen();
    }

    void opened(int32_t) override { }

    void closed(int32_t id) override
    {
        if (MenuObject *menu = menuFor(id))
            menu->closed();
    }

    void activate(int32_t id) override
    {
        const auto it = itemsById().find(id);
        if (id != 0 && it != itemsById().end()) {
            wl_resource *resource = it->second->resource();
            wl_client *client = wl_resource_get_client(resource);
            cutefish_menu_item_v1_send_activated(resource, choiceSerial(client));
            wl_client_flush(client);
        }
    }

private:
    MenuObject *menuFor(int32_t id) const
    {
        if (id == 0)
            return m_menu;
        const auto it = itemsById().find(id);
        return it == itemsById().end() ? nullptr : it->second->submenu();
    }

    MenuObject *m_menu;
    compositor::MenuItem m_root;
    std::vector<compositor::Surface *> m_surfaces;
};

ItemObject::ItemObject(wl_resource *resource, int32_t id, bool separator)
    : m_resource(resource)
{
    m_data.id = id;
    m_data.separator = separator;
    itemsById()[id] = this;
}

ItemObject::~ItemObject()
{
    if (m_menu)
        m_menu->remove(this);
    if (m_submenu)
        m_submenu->setOwner(nullptr);
    itemsById().erase(m_data.id);
}

void ItemObject::setMenu(MenuObject *menu)
{
    m_menu = menu;
    updateStructure();
}

void ItemObject::setSubmenu(MenuObject *submenu)
{
    if (m_submenu)
        m_submenu->setOwner(nullptr);
    m_submenu = submenu;
    if (m_submenu)
        m_submenu->setOwner(this);
    updateStructure();
    changed();
}

void ItemObject::updateStructure()
{
    m_data.parent = m_menu && m_menu->owner() ? m_menu->owner()->data().id : 0;
    m_data.hasSubmenu = m_submenu != nullptr;
    m_data.childrenLoaded = m_submenu != nullptr;
    m_data.role = m_submenu ? m_submenu->role() : compositor::MenuItem::Role::None;
    m_data.children.clear();
    if (m_submenu) {
        for (ItemObject *child : m_submenu->items())
            m_data.children.push_back(child->data().id);
    }
}

void ItemObject::changed()
{
    if (!m_menu)
        return;
    for (const std::shared_ptr<RootModel> &root : m_menu->roots())
        root->itemChanged(m_data.id);
}

MenuObject::~MenuObject()
{
    for (ItemObject *item : m_items)
        item->setMenu(nullptr);
    m_items.clear();
    if (m_owner)
        m_owner->setSubmenu(nullptr);
    if (const std::shared_ptr<RootModel> root = m_root.lock())
        root->detach();
}

void MenuObject::setOwner(ItemObject *owner)
{
    m_owner = owner;
    for (ItemObject *item : m_items)
        item->updateStructure();
}

void MenuObject::insert(ItemObject *item, size_t index)
{
    m_items.insert(m_items.begin() + std::ptrdiff_t(index), item);
    item->setMenu(this);
    structureChanged();
}

void MenuObject::remove(ItemObject *item)
{
    std::erase(m_items, item);
    item->setMenu(nullptr);
    structureChanged();
}

void MenuObject::setRole(compositor::MenuItem::Role role)
{
    if (role == m_role)
        return;
    m_role = role;
    if (m_owner) {
        m_owner->updateStructure();
        m_owner->changed();
    }
}

void MenuObject::willOpen()
{
    const uint32_t serial = wl_display_next_serial(wl_client_get_display(wl_resource_get_client(m_resource)));
    m_pendingSerials.insert(serial);
    cutefish_menu_v1_send_will_open(m_resource, serial);
    wl_client_flush(wl_resource_get_client(m_resource));
}

bool MenuObject::ready(uint32_t serial)
{
    if (!m_pendingSerials.erase(serial))
        return false;
    for (const std::shared_ptr<RootModel> &root : roots())
        root->menuUpdated(modelId());
    return true;
}

void MenuObject::closed()
{
    cutefish_menu_v1_send_closed(m_resource);
    wl_client_flush(wl_resource_get_client(m_resource));
}

std::shared_ptr<RootModel> MenuObject::rootModel()
{
    std::shared_ptr<RootModel> root = m_root.lock();
    if (!root) {
        root = std::make_shared<RootModel>(this);
        m_root = root;
    }
    return root;
}

std::vector<std::shared_ptr<RootModel>> MenuObject::roots()
{
    std::vector<std::shared_ptr<RootModel>> found;
    // Submenus never contain the items above them, so the walk ends.
    for (MenuObject *menu = this; menu; menu = menu->m_owner ? menu->m_owner->menu() : nullptr) {
        if (std::shared_ptr<RootModel> root = menu->m_root.lock())
            found.push_back(std::move(root));
    }
    return found;
}

int32_t MenuObject::modelId() const
{
    return m_owner ? m_owner->data().id : 0;
}

void MenuObject::structureChanged()
{
    if (m_owner)
        m_owner->updateStructure();
    for (const std::shared_ptr<RootModel> &root : roots()) {
        if (root->menu() == this)
            root->updateRoot();
        root->menuUpdated(modelId());
    }
}

// Whether `menu` is `item`'s menu or one above it.
bool contains(MenuObject *menu, ItemObject *item)
{
    for (MenuObject *above = item->menu(); above; above = above->owner() ? above->owner()->menu() : nullptr) {
        if (above == menu)
            return true;
    }
    return false;
}

std::string escapedTitle(const char *title)
{
    std::string text(title);
    if (text.size() > kMaxTitleBytes)
        text.resize(kMaxTitleBytes);
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        escaped += c;
        if (c == '_')
            escaped += '_';
    }
    return escaped;
}

// cutefish_menu_item_v1

void itemDestroy(wl_client *, wl_resource *resource) { wl_resource_destroy(resource); }

void itemSetTitle(wl_client *, wl_resource *resource, const char *title)
{
    ItemObject *item = getUserDataAs<ItemObject>(resource);
    item->data().label = escapedTitle(title);
    item->changed();
}

void itemSetEnabled(wl_client *, wl_resource *resource, uint32_t enabled)
{
    if (enabled > 1) {
        wl_resource_post_error(resource, CUTEFISH_MENU_ITEM_V1_ERROR_INVALID_VALUE, "enabled is 0 or 1");
        return;
    }
    ItemObject *item = getUserDataAs<ItemObject>(resource);
    item->data().enabled = enabled != 0;
    item->changed();
}

void itemSetHidden(wl_client *, wl_resource *resource, uint32_t hidden)
{
    if (hidden > 1) {
        wl_resource_post_error(resource, CUTEFISH_MENU_ITEM_V1_ERROR_INVALID_VALUE, "hidden is 0 or 1");
        return;
    }
    ItemObject *item = getUserDataAs<ItemObject>(resource);
    item->data().visible = hidden == 0;
    item->changed();
}

void itemSetState(wl_client *, wl_resource *resource, uint32_t state)
{
    if (state > CUTEFISH_MENU_ITEM_V1_STATE_MIXED) {
        wl_resource_post_error(resource, CUTEFISH_MENU_ITEM_V1_ERROR_INVALID_VALUE, "unknown state %u", state);
        return;
    }
    ItemObject *item = getUserDataAs<ItemObject>(resource);
    item->data().toggle = state == CUTEFISH_MENU_ITEM_V1_STATE_OFF ? compositor::MenuItem::Toggle::None
                                                                    : compositor::MenuItem::Toggle::Checkmark;
    item->data().toggleState = state == CUTEFISH_MENU_ITEM_V1_STATE_ON ? 1
            : state == CUTEFISH_MENU_ITEM_V1_STATE_OFF                ? 0
                                                                       : -1;
    item->changed();
}

void itemSetIconName(wl_client *, wl_resource *resource, const char *name)
{
    ItemObject *item = getUserDataAs<ItemObject>(resource);
    std::string icon(name);
    if (icon.size() > kMaxTitleBytes)
        icon.clear();
    item->data().iconName = std::move(icon);
    item->changed();
}

void itemSetKeyEquivalent(wl_client *, wl_resource *resource, const char *key, uint32_t modifiers)
{
    constexpr uint32_t kKnownModifiers = CUTEFISH_MENU_ITEM_V1_MODIFIER_SHIFT | CUTEFISH_MENU_ITEM_V1_MODIFIER_CONTROL
            | CUTEFISH_MENU_ITEM_V1_MODIFIER_ALT | CUTEFISH_MENU_ITEM_V1_MODIFIER_SUPER;
    if (modifiers & ~kKnownModifiers) {
        wl_resource_post_error(resource, CUTEFISH_MENU_ITEM_V1_ERROR_INVALID_VALUE, "unknown modifiers 0x%x",
                               modifiers);
        return;
    }
    ItemObject *item = getUserDataAs<ItemObject>(resource);
    std::vector<std::vector<std::string>> &shortcut = item->data().shortcut;
    shortcut.clear();
    const std::string name(key);
    if (!name.empty() && name.size() <= kMaxTitleBytes) {
        // In the order dbusmenu lists them.
        std::vector<std::string> keys;
        if (modifiers & CUTEFISH_MENU_ITEM_V1_MODIFIER_CONTROL)
            keys.push_back("Control");
        if (modifiers & CUTEFISH_MENU_ITEM_V1_MODIFIER_ALT)
            keys.push_back("Alt");
        if (modifiers & CUTEFISH_MENU_ITEM_V1_MODIFIER_SHIFT)
            keys.push_back("Shift");
        if (modifiers & CUTEFISH_MENU_ITEM_V1_MODIFIER_SUPER)
            keys.push_back("Super");
        keys.push_back(name);
        shortcut.push_back(std::move(keys));
    }
    item->changed();
}

void itemSetSubmenu(wl_client *, wl_resource *resource, wl_resource *menuResource)
{
    ItemObject *item = getUserDataAs<ItemObject>(resource);
    MenuObject *submenu = menuResource ? getUserDataAs<MenuObject>(menuResource) : nullptr;
    if (submenu == item->submenu())
        return;
    if (submenu && (submenu->owner() || contains(submenu, item))) {
        wl_resource_post_error(resource, CUTEFISH_MENU_ITEM_V1_ERROR_SUBMENU_IN_USE,
                               "the menu is another item's submenu or contains this item");
        return;
    }
    item->setSubmenu(submenu);
    if (MenuObject *menu = item->menu())
        menu->structureChanged();
}

const struct cutefish_menu_item_v1_interface itemImplementation = {
    itemDestroy, itemSetTitle, itemSetEnabled, itemSetHidden, itemSetState, itemSetIconName,
    itemSetKeyEquivalent, itemSetSubmenu,
};

// cutefish_menu_v1

void menuDestroy(wl_client *, wl_resource *resource) { wl_resource_destroy(resource); }

void menuInsertItem(wl_client *, wl_resource *resource, wl_resource *itemResource, uint32_t index)
{
    MenuObject *menu = getUserDataAs<MenuObject>(resource);
    ItemObject *item = getUserDataAs<ItemObject>(itemResource);
    if (item->menu()) {
        wl_resource_post_error(resource, CUTEFISH_MENU_V1_ERROR_ITEM_IN_USE, "the item is in a menu already");
        return;
    }
    if (index > menu->items().size()) {
        wl_resource_post_error(resource, CUTEFISH_MENU_V1_ERROR_INVALID_INDEX, "index %u past %zu items", index,
                               menu->items().size());
        return;
    }
    // A menu the item opens, or one under it, can not hold it.
    for (MenuObject *above = menu; above; above = above->owner() ? above->owner()->menu() : nullptr) {
        if (above == item->submenu()) {
            wl_resource_post_error(resource, CUTEFISH_MENU_V1_ERROR_ITEM_IN_USE, "the item opens this menu");
            return;
        }
    }
    if (menu->items().size() >= kMaxMenuItems)
        return;
    menu->insert(item, index);
}

void menuRemoveItem(wl_client *, wl_resource *resource, wl_resource *itemResource)
{
    MenuObject *menu = getUserDataAs<MenuObject>(resource);
    ItemObject *item = getUserDataAs<ItemObject>(itemResource);
    if (item->menu() != menu) {
        wl_resource_post_error(resource, CUTEFISH_MENU_V1_ERROR_NOT_AN_ITEM, "the item is not in this menu");
        return;
    }
    menu->remove(item);
}

void menuSetRole(wl_client *, wl_resource *resource, uint32_t role)
{
    if (role > CUTEFISH_MENU_V1_ROLE_SERVICES) {
        wl_resource_post_error(resource, CUTEFISH_MENU_V1_ERROR_INVALID_VALUE, "unknown role %u", role);
        return;
    }
    getUserDataAs<MenuObject>(resource)->setRole(compositor::MenuItem::Role(role));
}

void menuReady(wl_client *, wl_resource *resource, uint32_t serial)
{
    if (!getUserDataAs<MenuObject>(resource)->ready(serial))
        wl_resource_post_error(resource, CUTEFISH_MENU_V1_ERROR_INVALID_SERIAL, "serial %u was not sent", serial);
}

const struct cutefish_menu_v1_interface menuImplementation = {
    menuDestroy, menuInsertItem, menuRemoveItem, menuSetRole, menuReady,
};

// cutefish_main_menu_manager_v1

void managerDestroy(wl_client *, wl_resource *resource) { wl_resource_destroy(resource); }

void managerCreateMenu(wl_client *client, wl_resource *manager, uint32_t id)
{
    wl_resource *resource =
            wl_resource_create(client, &cutefish_menu_v1_interface, wl_resource_get_version(manager), id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    setImplementation(resource, &menuImplementation, std::make_unique<MenuObject>(resource));
}

void managerCreateItem(wl_client *client, wl_resource *manager, uint32_t id, uint32_t kind)
{
    wl_resource *resource =
            wl_resource_create(client, &cutefish_menu_item_v1_interface, wl_resource_get_version(manager), id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    setImplementation(resource, &itemImplementation,
                      std::make_unique<ItemObject>(resource, nextItemId(),
                                                   kind == CUTEFISH_MENU_ITEM_V1_KIND_SEPARATOR));
    if (kind > CUTEFISH_MENU_ITEM_V1_KIND_SEPARATOR)
        wl_resource_post_error(resource, CUTEFISH_MENU_ITEM_V1_ERROR_INVALID_VALUE, "unknown kind %u", kind);
}

void managerSetMainMenu(wl_client *, wl_resource *manager, wl_resource *surfaceResource, wl_resource *menuResource)
{
    compositor::Surface *surface = surfaceFromResource(surfaceResource);
    if (!surface || !toplevelShellSurfaceForSurface(surfaceResource)) {
        wl_resource_post_error(manager, CUTEFISH_MAIN_MENU_MANAGER_V1_ERROR_INVALID_SURFACE,
                               "the surface is not a toplevel");
        return;
    }
    if (!menuResource) {
        surface->setMainMenu(nullptr);
        return;
    }
    std::shared_ptr<RootModel> root = getUserDataAs<MenuObject>(menuResource)->rootModel();
    root->addSurface(surface);
    surface->setMainMenu(std::move(root));
}

const struct cutefish_main_menu_manager_v1_interface managerImplementation = {
    managerDestroy, managerCreateMenu, managerCreateItem, managerSetMainMenu,
};

void bindManager(wl_client *client, void *, uint32_t version, uint32_t id)
{
    wl_resource *resource = wl_resource_create(client, &cutefish_main_menu_manager_v1_interface,
                                               std::min(version, kMainMenuVersion), id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &managerImplementation, nullptr, nullptr);
}

} // namespace

wl_global *createMainMenuGlobal(wl_display *display, WaylandSeat *seat)
{
    menuSeat() = seat;
    return wl_global_create(display, &cutefish_main_menu_manager_v1_interface, kMainMenuVersion, nullptr,
                            bindManager);
}

} // namespace cutefish::wayland
