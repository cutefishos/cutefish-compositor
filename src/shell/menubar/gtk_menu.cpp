// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/menubar/gtk_menu.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <utility>

namespace cutefish::shell {

namespace dbus = base::dbus;

static constexpr const char *kMenusInterface = "org.gtk.Menus";
static constexpr const char *kActionsInterface = "org.gtk.Actions";

static std::vector<std::vector<std::string>> shortcutOf(const std::string &accelerator)
{
    std::vector<std::string> keys;
    size_t at = 0;
    while (at < accelerator.size() && accelerator[at] == '<') {
        const size_t end = accelerator.find('>', at);
        if (end == std::string::npos)
            return {};
        std::string modifier = accelerator.substr(at + 1, end - at - 1);
        std::transform(modifier.begin(), modifier.end(), modifier.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (modifier == "primary" || modifier == "control" || modifier == "ctrl" || modifier == "ctl")
            keys.push_back("Control");
        else if (modifier == "shift" || modifier == "shft")
            keys.push_back("Shift");
        else if (modifier == "alt" || modifier == "mod1")
            keys.push_back("Alt");
        else if (modifier == "super" || modifier == "meta" || modifier == "hyper")
            keys.push_back("Super");
        at = end + 1;
    }
    if (at >= accelerator.size())
        return {};
    keys.push_back(accelerator.substr(at));
    return {std::move(keys)};
}

// A serialized themed icon, ("themed", <["name", ...]>), or a bare name.
static std::string iconNameOf(const dbus::Variant &icon)
{
    if (const std::optional<std::string> name = icon.get<std::string>())
        return name->find('/') == std::string::npos ? *name : std::string();
    if (icon.signature() != "(sv)" || icon.items()[0].value<std::string>() != "themed")
        return {};
    const std::optional<dbus::Variant> names = icon.items()[1].get<dbus::Variant>();
    const std::vector<std::string> list = names ? names->value<std::vector<std::string>>() : std::vector<std::string>();
    return list.empty() ? std::string() : list.front();
}

static std::string stringAttribute(const dbus::VariantMap &attributes, const std::string &name)
{
    const auto it = attributes.find(name);
    return it == attributes.end() ? std::string() : it->second.value<std::string>();
}

// An item's "action-namespace" applies to its section or submenu, under the
// namespace it is in itself.
static std::string namespaceUnder(const std::string &outer, const dbus::VariantMap &attributes)
{
    const std::string inner = stringAttribute(attributes, "action-namespace");
    if (inner.empty())
        return outer;
    return outer.empty() ? inner : outer + '.' + inner;
}

static std::string qualified(const std::string &actionNamespace, const std::string &name)
{
    if (name.empty() || actionNamespace.empty())
        return name;
    return actionNamespace + '.' + name;
}

static bool sameProperties(const compositor::MenuItem &a, const compositor::MenuItem &b)
{
    return a.label == b.label && a.enabled == b.enabled && a.visible == b.visible && a.iconName == b.iconName
            && a.shortcut == b.shortcut && a.toggle == b.toggle && a.toggleState == b.toggleState
            && a.hasSubmenu == b.hasSubmenu && a.separator == b.separator && a.role == b.role;
}

GtkMenu::GtkMenu(dbus::Connection *bus, compositor::GtkMenuAddress address)
    : m_bus(bus)
    , m_address(std::move(address))
    , m_sender(bus, m_address.busName)
{
    compositor::MenuItem root;
    root.hasSubmenu = true;
    m_items.emplace(0, std::move(root));

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.onTimeout = [this] { rebuild(); };
    m_preparedTimer.setSingleShot(true);
    m_preparedTimer.onTimeout = [this] {
        for (int32_t id : std::exchange(m_prepared, {})) {
            if (m_items.count(id))
                menuUpdated(id);
        }
    };

    const std::array<const std::string *, kExporterCount> paths = {&m_address.menubarPath, &m_address.appMenuPath};
    for (int exporter = 0; exporter < kExporterCount; ++exporter) {
        if (paths[exporter]->empty())
            continue;
        m_menuProxies[exporter] = dbus::Proxy(bus, m_address.busName, *paths[exporter], kMenusInterface);
        m_matches.push_back(m_menuProxies[exporter].onSignal(
                "Changed", [this, exporter](const dbus::Message &message) { onMenusChanged(exporter, message); }));
        start(exporter, {0});
    }
    for (const auto &[prefix, path] : {std::pair<std::string, std::string>("app", m_address.applicationPath),
                                       std::pair<std::string, std::string>("win", m_address.windowPath)}) {
        if (path.empty())
            continue;
        const dbus::Proxy &proxy =
                m_actionProxies.emplace(prefix, dbus::Proxy(bus, m_address.busName, path, kActionsInterface))
                        .first->second;
        m_matches.push_back(proxy.onSignal("Changed", [this, prefix](const dbus::Message &message) {
            onActionsChanged(prefix, message);
        }));
        describeActions(prefix);
    }
    if (m_loading == 0)
        scheduleRebuild();
}

GtkMenu::~GtkMenu()
{
    for (int exporter = 0; exporter < kExporterCount; ++exporter) {
        const std::set<uint32_t> &groups = m_subscribed[exporter];
        if (m_menuProxies[exporter].isValid() && !groups.empty())
            m_menuProxies[exporter].send("End", std::vector<uint32_t>(groups.begin(), groups.end()));
    }
}

const compositor::MenuItem *GtkMenu::item(int32_t id) const
{
    const auto it = m_items.find(id);
    return it == m_items.end() ? nullptr : &it->second;
}

GtkMenu::Entry GtkMenu::parseEntry(const dbus::VariantMap &item)
{
    Entry entry;
    for (const auto &[key, value] : item) {
        if (key == ":section")
            entry.section = value.get<Link>();
        else if (key == ":submenu")
            entry.submenu = value.get<Link>();
        else if (!key.starts_with(':'))
            entry.attributes.emplace(key, value);
    }
    return entry;
}

GtkMenu::Action GtkMenu::parseAction(const ActionDescription &description)
{
    Action action;
    action.enabled = std::get<0>(description);
    action.parameterType = std::get<1>(description).signature;
    if (!std::get<2>(description).empty())
        action.state = std::get<2>(description).front();
    return action;
}

bool GtkMenu::loaded(int exporter, uint32_t group) const
{
    return m_loaded[exporter].count(group) > 0;
}

void GtkMenu::start(int exporter, std::vector<uint32_t> requested)
{
    std::vector<uint32_t> groups;
    for (uint32_t group : requested) {
        if (m_subscribed[exporter].size() < kMaxGroups && m_subscribed[exporter].insert(group).second)
            groups.push_back(group);
    }
    if (groups.empty())
        return;

    const bool initial = !m_built;
    if (initial)
        ++m_loading;
    m_calls.track([&](std::function<void()> finished) {
        return m_menuProxies[exporter].callAsync(
                "Start",
                [this, exporter, groups, initial, finished](const dbus::Reply &reply) {
                    std::vector<std::tuple<uint32_t, uint32_t, std::vector<dbus::VariantMap>>> menus;
                    std::vector<Entry> linked;
                    if (reply.read(menus)) {
                        for (const auto &[group, menu, items] : menus) {
                            if (std::find(groups.begin(), groups.end(), group) == groups.end())
                                continue;
                            std::vector<Entry> entries;
                            for (const dbus::VariantMap &item : items) {
                                if (entries.size() >= kMaxItems)
                                    break;
                                entries.push_back(parseEntry(item));
                            }
                            linked.insert(linked.end(), entries.begin(), entries.end());
                            m_menus[{exporter, group, menu}] = std::move(entries);
                        }
                    }
                    m_loaded[exporter].insert(groups.begin(), groups.end());
                    subscribeLinks(exporter, linked);
                    if (initial)
                        --m_loading;
                    scheduleRebuild();
                    finished();
                },
                groups);
    });
}

void GtkMenu::subscribeLinks(int exporter, const std::vector<Entry> &entries)
{
    std::vector<uint32_t> groups;
    for (const Entry &entry : entries) {
        for (const std::optional<Link> &link : {entry.section, entry.submenu}) {
            if (!link)
                continue;
            const uint32_t group = std::get<0>(*link);
            if (!m_subscribed[exporter].count(group) && std::find(groups.begin(), groups.end(), group) == groups.end())
                groups.push_back(group);
        }
    }
    if (!groups.empty())
        start(exporter, std::move(groups));
}

void GtkMenu::describeActions(const std::string &prefix)
{
    const bool initial = !m_built;
    if (initial)
        ++m_loading;
    m_calls.track([&](std::function<void()> finished) {
        return m_actionProxies.at(prefix).callAsync(
                "DescribeAll",
                [this, prefix, initial, finished](const dbus::Reply &reply) {
                    std::map<std::string, ActionDescription> described;
                    if (reply.read(described)) {
                        std::map<std::string, Action> &actions = m_actions[prefix];
                        actions.clear();
                        for (const auto &[name, description] : described) {
                            if (actions.size() >= kMaxItems)
                                break;
                            actions.emplace(name, parseAction(description));
                        }
                    }
                    if (initial)
                        --m_loading;
                    scheduleRebuild();
                    finished();
                });
    });
}

void GtkMenu::onMenusChanged(int exporter, const dbus::Message &message)
{
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, std::vector<dbus::VariantMap>>> changes;
    if (!m_sender.accepts(message) || !message.read(changes))
        return;
    std::vector<Entry> linked;
    for (const auto &[group, menu, position, removed, added] : changes) {
        auto it = m_menus.find({exporter, group, menu});
        if (it == m_menus.end()) {
            if (!loaded(exporter, group))
                continue;
            it = m_menus.emplace(MenuKey{exporter, group, menu}, std::vector<Entry>()).first;
        }
        std::vector<Entry> &entries = it->second;
        const size_t at = std::min<size_t>(position, entries.size());
        const size_t count = std::min<size_t>(removed, entries.size() - at);
        entries.erase(entries.begin() + ptrdiff_t(at), entries.begin() + ptrdiff_t(at + count));
        std::vector<Entry> inserted;
        for (const dbus::VariantMap &item : added) {
            if (entries.size() + inserted.size() >= kMaxItems)
                break;
            inserted.push_back(parseEntry(item));
        }
        entries.insert(entries.begin() + ptrdiff_t(at), inserted.begin(), inserted.end());
        linked.insert(linked.end(), inserted.begin(), inserted.end());
    }
    subscribeLinks(exporter, linked);
    scheduleRebuild();
}

void GtkMenu::onActionsChanged(const std::string &prefix, const dbus::Message &message)
{
    std::vector<std::string> removed;
    std::map<std::string, bool> enabled;
    dbus::VariantMap states;
    std::map<std::string, ActionDescription> added;
    if (!m_sender.accepts(message) || !message.read(removed, enabled, states, added))
        return;
    std::map<std::string, Action> &actions = m_actions[prefix];
    for (const std::string &name : removed)
        actions.erase(name);
    for (const auto &[name, value] : enabled) {
        if (const auto it = actions.find(name); it != actions.end())
            it->second.enabled = value;
    }
    for (const auto &[name, value] : states) {
        if (const auto it = actions.find(name); it != actions.end())
            it->second.state = value;
    }
    for (const auto &[name, description] : added) {
        if (actions.size() < kMaxItems || actions.count(name))
            actions.insert_or_assign(name, parseAction(description));
    }
    scheduleRebuild();
}

void GtkMenu::scheduleRebuild()
{
    if (!m_rebuildTimer.isActive())
        m_rebuildTimer.start(std::chrono::nanoseconds(0));
}

int32_t GtkMenu::idFor(const std::string &key)
{
    const auto [it, inserted] = m_ids.emplace(key, m_nextId);
    if (inserted)
        ++m_nextId;
    return it->second;
}

const GtkMenu::Action *GtkMenu::findAction(const std::string &name) const
{
    const size_t dot = name.find('.');
    if (dot == std::string::npos)
        return nullptr;
    const auto group = m_actions.find(name.substr(0, dot));
    if (group == m_actions.end())
        return nullptr;
    const auto it = group->second.find(name.substr(dot + 1));
    return it == group->second.end() ? nullptr : &it->second;
}

// An action whose parameter type does not match the target is disabled; a
// target with a state is a radio item, a boolean state without one a check
// item.
void GtkMenu::applyAction(compositor::MenuItem &item, const Entry &entry, const Target &target) const
{
    if (target.action.empty())
        return;
    const std::string hiddenWhen = stringAttribute(entry.attributes, "hidden-when");
    const Action *action = findAction(target.action);
    if (!action) {
        item.enabled = false;
        item.visible = hiddenWhen != "action-missing" && hiddenWhen != "action-disabled";
        return;
    }
    const bool canActivate = target.parameter ? action->parameterType == target.parameter->signature()
                                              : action->parameterType.empty();
    item.enabled = canActivate && action->enabled;
    if (hiddenWhen == "action-disabled")
        item.visible = item.enabled;
    if (!canActivate || !action->state)
        return;
    if (target.parameter) {
        item.toggle = compositor::MenuItem::Toggle::Radio;
        item.toggleState = *action->state == *target.parameter ? 1 : 0;
    } else if (const std::optional<bool> state = action->state->get<bool>()) {
        item.toggle = compositor::MenuItem::Toggle::Checkmark;
        item.toggleState = *state ? 1 : 0;
    }
}

void GtkMenu::flatten(int exporter, uint32_t group, uint32_t menu, const std::string &actionNamespace,
                      const std::string &key, int depth, int32_t parent,
                      std::unordered_map<int32_t, compositor::MenuItem> &items,
                      std::unordered_map<int32_t, Target> &targets)
{
    // Links may form a cycle.
    if (depth > kMaxDepth)
        return;
    const auto it = m_menus.find({exporter, group, menu});
    if (it == m_menus.end())
        return;

    const auto addSeparator = [&](const std::string &separatorKey) {
        if (parent == 0 || items.size() >= kMaxItems)
            return;
        compositor::MenuItem separator;
        separator.id = idFor(separatorKey);
        separator.parent = parent;
        separator.separator = true;
        items.at(parent).children.push_back(separator.id);
        items.emplace(separator.id, std::move(separator));
    };

    for (size_t index = 0; index < it->second.size(); ++index) {
        const Entry &entry = it->second[index];
        const std::string itemKey = key + '/' + std::to_string(index);
        if (entry.section) {
            addSeparator(itemKey + '<');
            flatten(exporter, std::get<0>(*entry.section), std::get<1>(*entry.section),
                    namespaceUnder(actionNamespace, entry.attributes), itemKey, depth + 1, parent, items, targets);
            addSeparator(itemKey + '>');
            continue;
        }
        if (items.size() >= kMaxItems)
            return;

        compositor::MenuItem item;
        item.id = idFor(itemKey);
        item.parent = parent;
        item.label = stringAttribute(entry.attributes, "label");
        item.shortcut = shortcutOf(stringAttribute(entry.attributes, "accel"));
        if (const auto icon = entry.attributes.find("icon"); icon != entry.attributes.end())
            item.iconName = iconNameOf(icon->second);

        Target target;
        target.action = qualified(actionNamespace, stringAttribute(entry.attributes, "action"));
        if (const auto parameter = entry.attributes.find("target"); parameter != entry.attributes.end())
            target.parameter = parameter->second;
        applyAction(item, entry, target);

        const int32_t id = item.id;
        items.at(parent).children.push_back(id);
        if (entry.submenu) {
            const std::string submenuNamespace = namespaceUnder(actionNamespace, entry.attributes);
            target.submenuAction = qualified(submenuNamespace, stringAttribute(entry.attributes, "submenu-action"));
            item.hasSubmenu = true;
            item.childrenLoaded = loaded(exporter, std::get<0>(*entry.submenu));
            items.emplace(id, std::move(item));
            flatten(exporter, std::get<0>(*entry.submenu), std::get<1>(*entry.submenu), submenuNamespace, itemKey,
                    depth + 1, id, items, targets);
        } else {
            items.emplace(id, std::move(item));
        }
        targets.emplace(id, std::move(target));
    }
}

void GtkMenu::rebuild()
{
    if (m_loading > 0)
        return;
    m_built = true;

    std::unordered_map<int32_t, compositor::MenuItem> items;
    std::unordered_map<int32_t, Target> targets;
    compositor::MenuItem root;
    root.hasSubmenu = true;
    root.childrenLoaded = true;
    items.emplace(0, std::move(root));

    if (const auto menu = m_menus.find({kAppMenu, 0, 0}); menu != m_menus.end() && !menu->second.empty()) {
        compositor::MenuItem application;
        application.id = idFor("a");
        application.parent = 0;
        application.hasSubmenu = true;
        application.childrenLoaded = true;
        application.role = compositor::MenuItem::Role::Application;
        const int32_t id = application.id;
        items.emplace(id, std::move(application));
        items.at(0).children.push_back(id);
        flatten(kAppMenu, 0, 0, {}, "a", 1, id, items, targets);
    }
    flatten(kMenuBar, 0, 0, {}, "m", 0, 0, items, targets);

    std::vector<int32_t> updated;
    std::vector<int32_t> changed;
    for (const auto &[id, item] : items) {
        const auto old = m_items.find(id);
        if (old == m_items.end())
            continue;
        if (old->second.children != item.children || old->second.childrenLoaded != item.childrenLoaded)
            updated.push_back(id);
        if (!sameProperties(old->second, item))
            changed.push_back(id);
    }
    m_items = std::move(items);
    m_targets = std::move(targets);
    std::erase_if(m_ids, [this](const auto &entry) { return !m_items.count(entry.second); });
    std::erase_if(m_open, [this](int32_t id) { return !m_items.count(id); });

    for (int32_t id : changed) {
        if (m_items.count(id))
            itemChanged(id);
    }
    std::sort(updated.begin(), updated.end());
    for (int32_t id : updated) {
        if (m_items.count(id))
            menuUpdated(id);
    }
}

void GtkMenu::refresh(int32_t id)
{
    prepare(id);
}

void GtkMenu::prepare(int32_t id)
{
    const auto it = m_items.find(id);
    if (it == m_items.end() || !it->second.childrenLoaded)
        return;
    m_prepared.insert(id);
    if (!m_preparedTimer.isActive())
        m_preparedTimer.start(std::chrono::nanoseconds(0));
}

void GtkMenu::setActionState(const std::string &name, bool state)
{
    const size_t dot = name.find('.');
    if (dot == std::string::npos)
        return;
    const auto proxy = m_actionProxies.find(name.substr(0, dot));
    if (proxy != m_actionProxies.end())
        proxy->second.send("SetState", name.substr(dot + 1), dbus::Variant(state), dbus::VariantMap());
}

void GtkMenu::opened(int32_t id)
{
    m_open.insert(id);
    if (const auto it = m_targets.find(id); it != m_targets.end() && !it->second.submenuAction.empty())
        setActionState(it->second.submenuAction, true);
}

void GtkMenu::closed(int32_t id)
{
    m_open.erase(id);
    if (const auto it = m_targets.find(id); it != m_targets.end() && !it->second.submenuAction.empty())
        setActionState(it->second.submenuAction, false);
}

void GtkMenu::activate(int32_t id)
{
    const auto it = m_targets.find(id);
    if (it == m_targets.end() || it->second.action.empty())
        return;
    const std::string &name = it->second.action;
    const size_t dot = name.find('.');
    if (dot == std::string::npos)
        return;
    const auto proxy = m_actionProxies.find(name.substr(0, dot));
    if (proxy == m_actionProxies.end())
        return;
    std::vector<dbus::Variant> parameter;
    if (it->second.parameter)
        parameter.push_back(*it->second.parameter);
    proxy->second.send("Activate", name.substr(dot + 1), parameter, dbus::VariantMap());
}

} // namespace cutefish::shell
