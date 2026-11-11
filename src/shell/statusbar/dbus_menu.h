// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_STATUSBAR_DBUS_MENU_H_
#define CUTEFISH_SHELL_STATUSBAR_DBUS_MENU_H_

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/dbus.h"
#include "compositor/menu_model.h"
#include "base/signal.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "shell/common/pending_calls.h"
#include "shell/common/sender_filter.h"

namespace cutefish::shell {

// A client of a com.canonical.dbusmenu exporter, written from the protocol:
// the layout fetched one level at a time as menus open, kept current from
// LayoutUpdated and ItemsPropertiesUpdated, and the events a menu sends back.
// Nothing blocks; untrusted sizes are capped.
//
// An entry keeps the kind it had when it
// first arrived (type, toggle-type and children-display are not changed by a
// later layout), a submenu that opens is asked AboutToShow and refetched when
// the application says so or its entries are not current, and "opened",
// "closed" and "clicked" events go along. A LayoutUpdated makes everything
// under its parent out of date: open menus are refetched at once, the others
// when they next show.
class DBusMenu : public compositor::MenuModel
{
public:
    // Past these, the rest of a layout is dropped.
    static constexpr size_t kMaxItems = 4096;
    static constexpr size_t kMaxLabelBytes = 4096;
    static constexpr size_t kMaxIconBytes = 1 << 20;
    static constexpr int kMaxIconSide = 512;

    DBusMenu(base::dbus::Connection *bus, std::string service, std::string path);
    ~DBusMenu();

    DBusMenu(const DBusMenu &) = delete;
    DBusMenu &operator=(const DBusMenu &) = delete;

    const std::string &service() const { return m_service; }
    const std::string &path() const { return m_path; }

    const compositor::MenuItem *item(int32_t id) const override;
    void refresh(int32_t id) override;
    // AboutToShow; the entries are refetched when needed.
    void prepare(int32_t id) override;
    // The "opened" event.
    void opened(int32_t id) override;
    void closed(int32_t id) override;
    void activate(int32_t id) override;

private:
    void applyLayout(int32_t parentId, const base::dbus::Variant &layout);
    void applyProperties(compositor::MenuItem &item, const base::dbus::VariantMap &properties, bool created);
    void resetProperty(compositor::MenuItem &item, const std::string &key);
    void removeItem(int32_t id);
    void invalidate(int32_t id);
    void decodeIcon(int32_t id, const std::string &bytes);
    void sendEvent(int32_t id, const std::string &event);

    base::dbus::Connection *m_bus;
    std::string m_service;
    std::string m_path;
    base::dbus::Proxy m_proxy;
    SenderFilter m_sender;
    std::unordered_map<int32_t, compositor::MenuItem> m_items;
    std::vector<base::dbus::Match> m_matches;
    PendingCalls m_calls;
    std::set<int32_t> m_pendingLayoutUpdates;
    // The menus between "opened" and "closed".
    std::set<int32_t> m_open;
    base::Timer m_layoutTimer;
    std::unordered_map<int32_t, base::Job> m_iconJobs;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_STATUSBAR_DBUS_MENU_H_
