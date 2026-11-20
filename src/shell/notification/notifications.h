// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATIONS_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATIONS_H_

#include <memory>
#include <string>

#include "base/dbus.h"
#include "base/signal.h"
#include "shell/notification/notification_models.h"
#include "shell/notification/notification_server.h"
#include "shell/notification/notification_settings.h"

namespace cutefish::shell {

class ApplicationLauncher;
class Applications;
class TaskManager;

// Exports org.cutefish.Notification at /Notification, which Settings and the
// session call.
class Notifications
{
public:
    struct Files {
        std::string settings;
        std::string history;
        std::string images;

        static Files defaults();
    };

    // `launcher` starts a sender that has quit for an action on its
    // notification; may be null.
    Notifications(base::dbus::Connection *bus, Applications &applications, TaskManager *tasks,
                  Files files = Files::defaults(), ApplicationLauncher *launcher = nullptr);
    ~Notifications();

    Notifications(const Notifications &) = delete;
    Notifications &operator=(const Notifications &) = delete;

    // org.freedesktop.Notifications, then org.cutefish.Notification.
    bool registerServices();

    NotificationSettings &settings() { return m_settings; }
    NotificationHistory &history() { return m_history; }
    NotificationBanners &banners() { return m_banners; }

private:
    base::dbus::Connection *m_bus;
    NotificationSettings m_settings;
    NotificationServer m_server;
    NotificationHistory m_history;
    NotificationBanners m_banners;
    std::unique_ptr<base::dbus::Object> m_object;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATIONS_H_
