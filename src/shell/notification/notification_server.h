// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_SERVER_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_SERVER_H_

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/dbus.h"
#include "base/signal.h"
#include "base/thread_pool.h"
#include "shell/common/applications.h"
#include "shell/common/pending_calls.h"
#include "shell/notification/notification.h"
#include "shell/notification/notification_images.h"

namespace cutefish::shell {

class ApplicationLauncher;
class NotificationSettings;
class TaskManager;

// Notify() answers at once with the id; the rest happens without blocking, and
// notifications show in the order they came.
class NotificationServer
{
public:
    // Numbered by the spec.
    enum class CloseReason {
        Expired = 1,
        DismissedByUser = 2,
        Revoked = 3,
    };

    // Sent back to a sender's application started for an action, as
    // "<scheme>:<id>?action=<key>", both percent-encoded.
    static constexpr const char *kRelaunchHint = "x-cutefish-relaunch-id";
    static constexpr const char *kRelaunchScheme = "cutefish-notification";

    // `tasks` and `launcher` may be null.
    NotificationServer(base::dbus::Connection *bus, NotificationSettings &settings, Applications &applications,
                       TaskManager *tasks, std::string imageDirectory = notification_images::defaultDirectory(),
                       ApplicationLauncher *launcher = nullptr);
    ~NotificationServer();

    NotificationServer(const NotificationServer &) = delete;
    NotificationServer &operator=(const NotificationServer &) = delete;

    // Exports the object, then takes the name.
    bool registerService();

    const std::string &imageDirectory() const { return m_imageDirectory; }

    void invokeAction(uint32_t id, const std::string &actionKey);
    void closeNotification(uint32_t id, CloseReason reason);

    base::Signal<const Notification &> notificationAdded;
    base::Signal<uint32_t, const Notification &> notificationReplaced;
    base::Signal<uint32_t, CloseReason> notificationRemoved;

private:
    struct Pending {
        Notification notification;
        bool wasReplaced = false;
        std::optional<ImageHint> imageData;
        std::string imagePath;
        DesktopEntryPtr entry;
        std::string relaunchId;
        bool resolved = false;
        bool off = false;
        bool closeRequested = false;
        base::Job imageJob;
    };

    struct Sender {
        std::string service;
        DesktopEntryPtr entry;
        std::string relaunchId;
    };

    void notify(base::dbus::MethodCall &call);
    void relaunchIfGone(const Sender &sender, uint32_t id, const std::string &actionKey);
    void closeFromBus(base::dbus::MethodCall &call);
    void lookUpSender(Pending *pending);
    void decide(Pending *pending);
    void finishImages(Pending *pending);
    void flush();
    bool isOpen(uint32_t id) const;
    Pending *pendingFor(uint32_t id) const;
    bool isPending(const Pending *pending) const;
    DesktopEntryPtr visibleOwner(DesktopEntryPtr entry) const;

    base::dbus::Connection *m_bus;
    NotificationSettings &m_settings;
    Applications &m_applications;
    TaskManager *m_tasks;
    std::string m_imageDirectory;
    ApplicationLauncher *m_launcher;
    std::unique_ptr<base::dbus::Object> m_object;
    PendingCalls m_calls;
    uint32_t m_nextId = 1;
    Notification m_last;
    std::map<uint32_t, Sender> m_open;
    std::deque<std::unique_ptr<Pending>> m_pending;
    std::deque<Pending *> m_undecided;
    base::Subscription m_settingsReady;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_SERVER_H_
