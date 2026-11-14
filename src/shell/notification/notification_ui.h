// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_UI_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_UI_H_

#include <memory>

#include "base/signal.h"
#include "shell/shell.h"
#include "shell/menubar/menu_bar_panels.h"

namespace cutefish::shell {

class NotificationCentre;
class NotificationPopups;
class Notifications;

// The notifications component: the server, history and settings
// (Notifications) with their views, the banners and the notification
// centre. The menu bar opens the centre from its clock, as its
// NotificationCenter panel.
class NotificationUi : public Component, public MenuBarPanels
{
public:
    NotificationUi(Shell *shell, std::unique_ptr<Notifications> notifications);
    ~NotificationUi() override;

    // org.freedesktop.Notifications and org.cutefish.Notification.
    bool registerServices();

    Notifications &notifications() const { return *m_notifications; }
    NotificationPopups &popups() const { return *m_popups; }
    NotificationCentre &centre() const { return *m_centre; }

    // --- MenuBarPanels: the notification centre ---

    bool hasPanel(Panel panel) const override;
    void openPanel(Panel panel, ShellWindow *bar, const base::Rect &anchor) override;
    void closePanels() override;
    bool isOpen(Panel panel) const override;

private:
    std::unique_ptr<Notifications> m_notifications;
    std::unique_ptr<NotificationPopups> m_popups;
    std::unique_ptr<NotificationCentre> m_centre;
    base::Subscription m_openChanged;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_UI_H_
