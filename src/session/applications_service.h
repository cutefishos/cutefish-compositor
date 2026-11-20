// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "base/dbus.h"
#include <functional>
#include <memory>
#include <vector>

namespace cutefish::wm {
class QuitAll;
}

namespace cutefish::session {

class ApplicationsService
{
public:
    // `setEnding` puts the docks and panels away while applications quit. The session
    // bus when no connection is given.
    ApplicationsService(wm::QuitAll *quitAll, std::function<void(bool)> setEnding,
                        base::dbus::Connection *bus = nullptr);
    ApplicationsService(const ApplicationsService &) = delete;
    ApplicationsService &operator=(const ApplicationsService &) = delete;
    ~ApplicationsService();

    bool registerService();

private:
    // QuitAll replies once every application has quit, "quit", or once one
    // has kept the session from ending, "cancelled" or "unresponsive" with
    // its id. Concurrent callers share the answer.
    void quitAll(base::dbus::MethodCall &call);

    wm::QuitAll *m_quitAll;
    std::function<void(bool)> m_setEnding;
    base::dbus::Connection *m_bus;
    std::unique_ptr<base::dbus::Object> m_object;
    std::vector<base::dbus::MethodCall> m_pending;
    bool m_registered = false;
};

} // namespace cutefish::session
