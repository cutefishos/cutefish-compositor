// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_SETTINGS_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_SETTINGS_H_

#include <map>
#include <string>
#include <vector>

#include "base/signal.h"
#include "base/thread_pool.h"

namespace cutefish::shell {

class Applications;

// Stored in the [Notifications] group of shell.conf; nothing is decided before
// isReady().
class NotificationSettings
{
public:
    // The values are stored in shell.conf.
    enum ApplicationMode {
        Banners = 0,
        CenterOnly = 1,
        Off = 2,
    };

    struct Application {
        std::string key;
        std::string name;
        std::string icon;
        ApplicationMode mode = Banners;
    };

    // `applications` must outlive the settings.
    NotificationSettings(Applications &applications, std::string path);

    bool isReady() const { return m_ready; }

    bool doNotDisturb() const { return m_doNotDisturb; }
    void setDoNotDisturb(bool enabled);

    // An unknown key shows banners.
    ApplicationMode applicationMode(const std::string &key) const;
    void setApplicationMode(const std::string &key, int mode);

    void noteApplication(const std::string &key, const std::string &name, const std::string &icon);
    std::vector<Application> applications() const;

    base::Signal<> ready;
    base::Signal<> doNotDisturbChanged;
    base::Signal<> applicationsChanged;

private:
    struct Stored {
        bool doNotDisturb = false;
        std::map<std::string, Application> applications;
    };

    static Stored load(const std::string &path);
    static std::string applicationGroup(const std::string &key);
    bool isInstalled(const std::string &key) const;

    Applications &m_applications;
    std::string m_path;
    bool m_ready = false;
    bool m_doNotDisturb = false;
    std::map<std::string, Application> m_entries;
    base::Job m_load;
    base::Subscription m_applicationsChanged;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_SETTINGS_H_
