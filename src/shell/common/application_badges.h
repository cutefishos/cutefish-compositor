// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_APPLICATION_BADGES_H_
#define CUTEFISH_SHELL_COMMON_APPLICATION_BADGES_H_

#include <cstdint>
#include <map>
#include <string>

#include "base/dbus.h"
#include "base/signal.h"

namespace cutefish::shell {

// Counts from com.canonical.Unity.LauncherEntry's Update signal, shown while
// count-visible is true and the count is above zero.
class ApplicationBadges
{
public:
    // `bus` may be null.
    explicit ApplicationBadges(base::dbus::Connection *bus);
    ~ApplicationBadges();

    ApplicationBadges(const ApplicationBadges &) = delete;
    ApplicationBadges &operator=(const ApplicationBadges &) = delete;

    // Ignores case; 0 for none.
    int64_t count(const std::string &desktopId) const;

    // The desktop id of a launcher URI; empty for any other URI.
    static std::string desktopId(const std::string &uri);

    base::Signal<> changed;

private:
    struct Entry {
        int64_t count = 0;
        bool visible = false;
        std::string sender;

        int64_t shown() const { return visible && count > 0 ? count : 0; }
    };

    void update(const std::string &sender, const std::string &uri, const base::dbus::VariantMap &properties);
    void senderVanished(const std::string &sender);

    // By lower-case desktop id.
    std::map<std::string, Entry> m_entries;
    base::dbus::Match m_update;
    base::dbus::Match m_nameOwnerChanged;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_APPLICATION_BADGES_H_
