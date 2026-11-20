// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_POPUPS_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_POPUPS_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "base/timer.h"
#include "shell/notification/notification.h"

namespace cutefish::shell {

class NotificationBanners;
class NotificationCard;
class Shell;
class ShellWindow;
class Tween;

class NotificationPopups
{
public:
    static constexpr int kWidth = 380;
    static constexpr int kEdgeDistance = 6;
    static constexpr int kSpacing = 9;
    static constexpr double kMaximumScreenFill = 0.6;
    static constexpr double kRadius = 18;
    static constexpr std::chrono::milliseconds kDefaultTimeout {7000};
    static constexpr std::chrono::milliseconds kMinimumTimeout {3000};

    class Banner
    {
    public:
        Banner(NotificationPopups *popups, const Notification &notification);
        ~Banner();

        uint32_t id() const { return m_notification.id; }
        const Notification &notification() const { return m_notification; }
        void setNotification(const Notification &notification);

        ShellWindow *window() const { return m_window.get(); }
        NotificationCard *card() const { return m_card; }
        bool isShown() const { return m_shown; }
        // Zero for a banner that stays until it is dismissed.
        std::chrono::milliseconds timeout() const;
        bool isTimerRunning() const;
        int height() const;
        base::Point stackPosition() const { return m_stack; }

        void place(const base::Point &position);
        void setShown(bool shown);
        void updateGlass();

    private:
        class Content;

        void updateTimer();
        void applyPosition();

        NotificationPopups *m_popups;
        Notification m_notification;
        std::unique_ptr<ShellWindow> m_window;
        NotificationCard *m_card = nullptr;
        std::unique_ptr<Tween> m_stackY;
        base::Point m_stack;
        bool m_placed = false;
        bool m_shown = false;
        bool m_timerRunning = false;
        base::Timer m_timer;
        int m_height = 0;
    };

    NotificationPopups(Shell *shell, NotificationBanners &model);
    ~NotificationPopups();

    NotificationPopups(const NotificationPopups &) = delete;
    NotificationPopups &operator=(const NotificationPopups &) = delete;

    const std::vector<std::unique_ptr<Banner>> &banners() const { return m_banners; }

    void positionPopups();

private:
    void insert(int first, int count);
    void remove(int first, int count);
    void update(int first, int last);
    void reset();
    void schedulePositioning();
    friend class Banner;

    Shell *m_shell;
    NotificationBanners &m_model;
    std::vector<std::unique_ptr<Banner>> m_banners;
    bool m_positionScheduled = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_POPUPS_H_
