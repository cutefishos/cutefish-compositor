// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_COMPUTER_CONTROL_REMEMBERED_APPS_H_
#define CUTEFISH_COMPUTER_CONTROL_REMEMBERED_APPS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/settings.h"
#include "base/signal.h"

namespace cutefish::input {
class DBusExport;
}

namespace cutefish::computer_control {

std::optional<int64_t> parentProcess(int64_t pid);

// `file` names that very file, so a program put in its place is another application.
struct AgentApplication {
    std::string path;
    std::string file;
};

// Kept in compositor.conf [ComputerControl].
class RememberedApps
{
public:
    explicit RememberedApps(const std::string &settingsFile);
    ~RememberedApps();

    RememberedApps(const RememberedApps &) = delete;
    RememberedApps &operator=(const RememberedApps &) = delete;

    bool registerService();

    // Empty when the parent can stand for any program: an interpreter, a shell or init.
    static std::optional<AgentApplication> applicationOf(int64_t pid);

    bool allows(const AgentApplication &application) const;
    void remember(const AgentApplication &application, const std::string &agentName,
                  const std::string &appId);
    void forget(const std::string &path);

    base::Signal<> changed;

private:
    struct Entry {
        std::string path;
        std::string file;
        std::string name;
        std::string appId;
    };

    void save();

    base::Settings m_settings;
    std::vector<Entry> m_entries;
    std::unique_ptr<input::DBusExport> m_object;
};

} // namespace cutefish::computer_control

#endif // CUTEFISH_COMPUTER_CONTROL_REMEMBERED_APPS_H_
