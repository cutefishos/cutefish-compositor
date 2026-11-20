// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_MODELS_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_MODELS_H_

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "base/signal.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "shell/common/list_model.h"
#include "shell/notification/notification.h"
#include "shell/notification/notification_server.h"

namespace cutefish::shell {

class NotificationSettings;

std::string friendlyTime(std::optional<int64_t> created, int64_t nowMs);

// Persisted in ~/.config/cutefish/notification-history.json, read and written
// on the file thread.
class NotificationHistory : public ListModelSignals
{
public:
    static constexpr size_t kLimit = 200;

    NotificationHistory(std::string path, std::string imageDirectory);
    ~NotificationHistory();

    NotificationHistory(const NotificationHistory &) = delete;
    NotificationHistory &operator=(const NotificationHistory &) = delete;

    static std::string defaultPath();

    bool isLoaded() const { return m_loaded; }
    const std::vector<Notification> &notifications() const { return m_notifications; }
    int count() const { return int(m_notifications.size()); }
    std::string createdText(int row) const;

    void add(const Notification &notification);
    void remove(std::vector<int> rows);
    void clearAll();
    void updateTime();

    base::Signal<> loaded;

private:
    void save();
    void removeImages(const Notification &notification);

    std::string m_path;
    std::string m_imageDirectory;
    std::vector<Notification> m_notifications;
    bool m_loaded = false;
    base::Job m_load;
};

class NotificationBanners : public ListModelSignals
{
public:
    static constexpr int kMaxBanners = 5;

    NotificationBanners(NotificationServer &server, NotificationSettings &settings, NotificationHistory &history);

    const std::vector<Notification> &notifications() const { return m_notifications; }
    int count() const { return int(m_notifications.size()); }
    int rowOf(uint32_t id) const;

    bool bannersSuppressed() const { return m_bannersSuppressed; }
    void setBannersSuppressed(bool suppressed);

    void expired(uint32_t id);
    void close(uint32_t id);
    void invokeDefaultAction(uint32_t id);
    void invokeAction(uint32_t id, const std::string &actionKey);

private:
    void add(const Notification &notification);
    void replace(uint32_t id, const Notification &notification);
    void onRemoved(uint32_t id, NotificationServer::CloseReason reason);
    void removePending();

    NotificationServer &m_server;
    NotificationSettings &m_settings;
    NotificationHistory &m_history;
    std::vector<Notification> m_notifications;
    // Rows go on the next turn of the loop: the banner that asked may still
    // be running the handler that got it closed.
    std::set<uint32_t> m_pendingRemovals;
    base::Timer m_removalTimer;
    bool m_bannersSuppressed = false;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_MODELS_H_
