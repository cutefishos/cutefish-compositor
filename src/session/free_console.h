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

// The display manager's terminals, the greeter's and the sessions', as
// cutefish-dm gives them in CUTEFISH_DM_VTS ("1,2"); none without it.
std::set<int> displayManagerConsoles(const char *variable);

// Whether a switch to `vt` may go ahead. A display manager's terminal with
// no session on it has nothing to show: the greeter's once it has gone,
// with getty kept off it; and a console signed in on the sessions' one
// would keep the next graphical login from starting there.
bool mayEnterConsole(int vt, const std::set<int> &displayManager, const std::set<int> &taken);

// The lowest of the prompting terminals no session is on and the display
// manager does not use, or 0 when there is none. Without cutefish-dm's
// list the first two are taken as the display manager's, as gdm uses them
// too.
int freeConsole(const std::set<int> &taken, const std::set<int> &displayManager);

// The terminals logind's sessions are on.
std::set<int> takenConsoles(base::dbus::Connection *bus);

} // namespace cutefish::session

#endif // CUTEFISH_SESSION_FREE_CONSOLE_H
