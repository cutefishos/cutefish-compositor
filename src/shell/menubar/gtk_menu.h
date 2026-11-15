// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_GTK_MENU_H_
#define CUTEFISH_SHELL_MENUBAR_GTK_MENU_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "base/dbus.h"
#include "base/timer.h"
#include "compositor/menu_model.h"
#include "shell/common/pending_calls.h"
#include "shell/common/sender_filter.h"

namespace cutefish::shell {

// A client of a window's org.gtk.Menus menu bar and application menu and of
// the org.gtk.Actions groups their items activate, written from the
// protocols. Every menu group is subscribed as soon as it is linked, so the
// whole tree stays current; sections are flattened between separators, and
// the application menu becomes the submenu of the application role.
// Enabled, check and radio state come from the actions, as GTK's own menus
// show them. Nothing blocks; untrusted sizes are capped.
class GtkMenu : public compositor::MenuModel
{
public:
    // Past these, the rest is dropped.
    static constexpr size_t kMaxItems = 4096;
    static constexpr size_t kMaxGroups = 512;
    static constexpr int kMaxDepth = 16;

    GtkMenu(base::dbus::Connection *bus, compositor::GtkMenuAddress address);
    ~GtkMenu();

    GtkMenu(const GtkMenu &) = delete;
    GtkMenu &operator=(const GtkMenu &) = delete;

    const compositor::GtkMenuAddress &address() const { return m_address; }

    const compositor::MenuItem *item(int32_t id) const override;
    void refresh(int32_t id) override;
    void prepare(int32_t id) override;
    // A submenu with a submenu action has it set while it is open.
    void opened(int32_t id) override;
    void closed(int32_t id) override;
    void activate(int32_t id) override;

private:
    // The menu bar's exporter, or the application menu's.
    enum Exporter { kMenuBar, kAppMenu, kExporterCount };
    using MenuKey = std::tuple<int, uint32_t, uint32_t>;
    using Link = std::tuple<uint32_t, uint32_t>;

    struct Entry {
        base::dbus::VariantMap attributes;
        std::optional<Link> section;
        std::optional<Link> submenu;
    };

    struct Action {
        bool enabled = true;
        std::string parameterType;
        std::optional<base::dbus::Variant> state;
    };

    // What an item activates, by its full name ("app.quit").
    struct Target {
        std::string action;
        std::optional<base::dbus::Variant> parameter;
        std::string submenuAction;
    };

    using ActionDescription = std::tuple<bool, base::dbus::Signature, std::vector<base::dbus::Variant>>;

    static Entry parseEntry(const base::dbus::VariantMap &item);
    static Action parseAction(const ActionDescription &description);

    void start(int exporter, std::vector<uint32_t> requested);
    void subscribeLinks(int exporter, const std::vector<Entry> &entries);
    void describeActions(const std::string &prefix);
    void onMenusChanged(int exporter, const base::dbus::Message &message);
    void onActionsChanged(const std::string &prefix, const base::dbus::Message &message);
    void scheduleRebuild();
    void rebuild();
    void flatten(int exporter, uint32_t group, uint32_t menu, const std::string &actionNamespace,
                 const std::string &key, int depth, int32_t parent,
                 std::unordered_map<int32_t, compositor::MenuItem> &items,
                 std::unordered_map<int32_t, Target> &targets);
    int32_t idFor(const std::string &key);
    void applyAction(compositor::MenuItem &item, const Entry &entry, const Target &target) const;
    const Action *findAction(const std::string &name) const;
    void setActionState(const std::string &name, bool state);
    bool loaded(int exporter, uint32_t group) const;

    base::dbus::Connection *m_bus;
    compositor::GtkMenuAddress m_address;
    SenderFilter m_sender;
    std::array<base::dbus::Proxy, kExporterCount> m_menuProxies;
    // By prefix: "app" and "win".
    std::map<std::string, base::dbus::Proxy> m_actionProxies;
    std::vector<base::dbus::Match> m_matches;
    PendingCalls m_calls;

    std::map<MenuKey, std::vector<Entry>> m_menus;
    // Groups asked for, and those whose reply is in.
    std::array<std::set<uint32_t>, kExporterCount> m_subscribed;
    std::array<std::set<uint32_t>, kExporterCount> m_loaded;
    std::map<std::string, std::map<std::string, Action>> m_actions;
    // The calls before the first build still on their way: the menu shows
    // once the whole tree is in.
    int m_loading = 0;
    bool m_built = false;

    std::unordered_map<int32_t, compositor::MenuItem> m_items;
    std::unordered_map<int32_t, Target> m_targets;
    std::unordered_map<std::string, int32_t> m_ids;
    int32_t m_nextId = 1;
    std::set<int32_t> m_open;
    std::set<int32_t> m_prepared;
    base::Timer m_rebuildTimer;
    base::Timer m_preparedTimer;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_GTK_MENU_H_
