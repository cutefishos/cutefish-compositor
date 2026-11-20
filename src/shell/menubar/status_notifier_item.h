// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_STATUS_NOTIFIER_ITEM_H_
#define CUTEFISH_SHELL_MENUBAR_STATUS_NOTIFIER_ITEM_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "shell/common/pending_calls.h"
#include "shell/common/sender_filter.h"
#include "ui/bitmap.h"

namespace cutefish::shell {

class DBusMenu;
class TaskManager;

class StatusNotifierItem
{
public:
    static constexpr int kMaxPixmapSide = 4096;
    static constexpr size_t kMaxPixmaps = 16;

    using IconExists = std::function<bool(const std::string &name, const std::string &themePath)>;

    // `id` is "service/path"; `tasks` may be null.
    StatusNotifierItem(base::dbus::Connection *bus, std::string id, TaskManager *tasks, IconExists iconExists);
    ~StatusNotifierItem();

    StatusNotifierItem(const StatusNotifierItem &) = delete;
    StatusNotifierItem &operator=(const StatusNotifierItem &) = delete;

    const std::string &id() const { return m_id; }
    const std::string &title() const { return m_title; }
    const std::string &toolTip() const { return m_toolTip; }
    // A theme name is kept, so a new icon theme applies.
    const std::string &iconName() const { return m_iconName; }
    const std::string &iconThemePath() const { return m_iconThemePath; }
    // Empty while decoding or when there are none.
    const std::vector<ui::Bitmap> &pixmaps() const { return m_pixmaps; }
    const std::string &applicationIcon() const { return m_applicationIcon; }

    bool itemIsMenu() const { return m_itemIsMenu; }
    bool isPassive() const { return m_passive; }

    // Null when the item has none; then requestContextMenu() asks the item to
    // show its own.
    DBusMenu *menu() const { return m_menu.get(); }

    // Logical global coordinates, only a hint for the application.
    void activate(int x, int y);
    void secondaryActivate(int x, int y);
    void scroll(int delta, const std::string &orientation);
    void requestContextMenu(int x, int y);

    void refresh();

    base::Signal<> changed;

private:
    void performRefresh();
    void apply(const base::dbus::VariantMap &properties);
    void decodePixmaps(const base::dbus::Variant &pixmaps);
    void lookUpApplicationIcon();

    base::dbus::Connection *m_bus;
    std::string m_id;
    std::string m_service;
    std::string m_path;
    TaskManager *m_tasks;
    IconExists m_iconExists;
    base::dbus::Proxy m_proxy;
    std::unique_ptr<SenderFilter> m_sender;
    std::vector<base::dbus::Match> m_matches;
    PendingCalls m_calls;
    base::Timer m_refreshTimer;
    bool m_refreshing = false;
    bool m_needsRefresh = false;

    std::string m_appId;
    std::string m_title;
    std::string m_toolTip;
    std::string m_iconName;
    std::string m_iconThemePath;
    std::vector<ui::Bitmap> m_pixmaps;
    std::string m_applicationIcon;
    bool m_itemIsMenu = false;
    bool m_passive = false;
    std::unique_ptr<DBusMenu> m_menu;
    base::Job m_decode;
    base::Subscription m_applicationsChanged;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_STATUS_NOTIFIER_ITEM_H_
