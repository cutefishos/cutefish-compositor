// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_CORE_CLIENT_WORK_H_
#define CUTEFISH_CORE_CLIENT_WORK_H_

namespace cutefish::core {

// Marks work done for clients on the main thread: dispatching their requests,
// or applying a commit once its fences have signalled. What changes in the
// scene meanwhile is theirs, not the compositor's own.
class ClientWorkScope
{
public:
    ClientWorkScope() { ++s_depth; }
    ~ClientWorkScope() { --s_depth; }

    ClientWorkScope(const ClientWorkScope &) = delete;
    ClientWorkScope &operator=(const ClientWorkScope &) = delete;

    static bool active() { return s_depth > 0; }

private:
    static inline int s_depth = 0;
};

} // namespace cutefish::core

#endif // CUTEFISH_CORE_CLIENT_WORK_H_
