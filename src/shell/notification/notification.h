// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/json.h"

namespace cutefish::shell {

struct Notification {
    uint32_t id = 0;
    // The sender's unique bus name.
    std::string service;
    std::string summary;
    std::string body;
    std::string appName;
    std::string appIcon;
    std::string desktopEntry;
    std::string image;
    // The spec's flat list: key, label, key, label, ...
    std::vector<std::string> actions;
    int32_t timeout = -1;
    int32_t urgency = 1;
    // Percent from the "value" hint, or -1.
    int32_t value = -1;
    // Milliseconds since the epoch, UTC.
    std::optional<int64_t> created;
    std::optional<int64_t> updated;

    struct Action {
        std::string key;
        std::string label;
    };

    // May be empty.
    std::string applicationKey() const;
    std::string groupKey() const;
    std::vector<Action> actionList() const;
    bool hasAction(const std::string &key) const;

    base::json::Value toJson() const;
    static Notification fromJson(const base::json::Value &value);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_H_
