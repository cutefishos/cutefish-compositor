// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "base/dbus.h"
#include "base/timer.h"
#include "session/session_state.h"

namespace cutefish::session {
class SessionController;

// A logind delay inhibitor holds sleep back until the lock is on every output, so
// waking never shows the desktop.
class SleepLock : public SessionObserver
{
public:
    // The system bus when no connection is given.
    explicit SleepLock(SessionController *session, base::dbus::Connection *bus = nullptr);
    SleepLock(const SleepLock &) = delete;
    SleepLock &operator=(const SleepLock &) = delete;
    ~SleepLock() override;

    bool start();

    bool holdsInhibitor() const { return m_inhibitor.isValid(); }

    void onSessionStateChanged(SessionState state) override;

private:
    void prepareForSleep(bool sleeping);
    void inhibit();
    void releaseWhenLocked();
    void updateLockedHint(SessionState state);

    SessionController *m_session;
    base::dbus::Connection *m_bus;
    base::dbus::Proxy m_manager;
    base::dbus::Proxy m_logindSession;
    base::dbus::Match m_prepareForSleep;
    base::dbus::PendingCall m_inhibiting;
    base::dbus::UnixFd m_inhibitor;
    // Not from inside the controller's own state change.
    base::Timer m_relock;
    bool m_sleeping = false;
    bool m_lockedHint = false;
    bool m_started = false;
};
} // namespace cutefish::session
