// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_PENDING_CALLS_H_
#define CUTEFISH_SHELL_COMMON_PENDING_CALLS_H_

#include <functional>
#include <list>
#include <utility>

#include "base/dbus.h"

namespace cutefish::shell {

// Pending replies are dropped with the owner, so none reaches a destroyed
// object.
class PendingCalls
{
public:
    // `start` must call `finished` last; a call that cannot be sent may answer
    // inside start().
    using Start = std::function<base::dbus::PendingCall(std::function<void()> finished)>;

    void track(const Start &start)
    {
        const auto call = m_calls.emplace(m_calls.end());
        base::dbus::PendingCall pending = start([this, call] {
            if (call->kept)
                m_calls.erase(call);
            else
                call->finished = true;
        });
        if (call->finished) {
            m_calls.erase(call);
            return;
        }
        call->pending = std::move(pending);
        call->kept = true;
    }

private:
    struct Call {
        base::dbus::PendingCall pending;
        bool kept = false;
        bool finished = false;
    };

    std::list<Call> m_calls;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_PENDING_CALLS_H_
