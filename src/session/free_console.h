// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SESSION_FREE_CONSOLE_H
#define CUTEFISH_SESSION_FREE_CONSOLE_H

#include <set>

namespace cutefish::base::dbus {
class Connection;
} // namespace cutefish::base::dbus

namespace cutefish::session {

// logind starts a login prompt on the first virtual terminals when one is
// switched to (NAutoVTs, 6 unless configured otherwise).
constexpr int kAutoConsoles = 6;

// The lowest of those no session is on, or 0 when all are taken. The first
// is the display manager's (cutefish-dm conflicts with getty@tty1, as gdm
// and others take it): after the greeter has gone it has no session, but no
// login prompt starts there either.
int freeConsole(const std::set<int> &taken);
// The terminals logind's sessions are on.
std::set<int> takenConsoles(base::dbus::Connection *bus);

} // namespace cutefish::session

#endif // CUTEFISH_SESSION_FREE_CONSOLE_H
