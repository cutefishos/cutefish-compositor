// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SESSION_SESSION_CONTROLLER_H_
#define CUTEFISH_SESSION_SESSION_CONTROLLER_H_

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "input/session_gate.h"
#include "session/secure_container.h"
#include "session/session_state.h"

namespace cutefish::compositor {
class Surface;
}

namespace cutefish::display {
struct AppliedConfiguration;
}

namespace cutefish::scene {
class Node;
}

namespace cutefish::session {

class SecureContainer;

class SessionController : public input::SessionGate
{
public:
    class Delegate
    {
    public:
        virtual ~Delegate() = default;

        virtual void onSessionBlocked() { }

        virtual void onSessionUnblocked() { }
    };

    explicit SessionController(SecureContainer *secure, Delegate *delegate = nullptr,
                               SessionState initial = SessionState::Active);
    SessionController(const SessionController &) = delete;
    SessionController &operator=(const SessionController &) = delete;
    ~SessionController() override;

    SessionState state() const { return m_state; }

    bool isBlocked() const { return isSessionBlocked(m_state); }

    SecureContainer *secureContainer() const { return m_secure; }

    bool requestLock();

    void notifyOutputPresented(std::string_view outputName);

    void notifyFramePresented();

    bool requestUnlock();

    void setOutputs(const std::vector<SecureOutput> &outputs);
    void setDisplayConfiguration(const display::AppliedConfiguration &configuration);

    bool allowsCapture() const { return !isBlocked(); }

    bool allowsDirectScanout() const { return !isBlocked(); }

    void addObserver(SessionObserver *observer);
    void removeObserver(SessionObserver *observer);

    bool restrictsInput() const override { return isBlocked(); }

    scene::Node *inputRoot() const override;

    input::EventSink *secureInput() const override { return m_authInput; }

    void setAuthInput(input::EventSink *input) { m_authInput = input; }

    bool allowsFocus(const compositor::Surface *surface) const override;
    bool allowsActivation() const override;

private:
    void setState(SessionState state);
    bool everyOutputPresented() const;
    void finishTransition();

    input::EventSink *m_authInput = nullptr;
    SecureContainer *m_secure = nullptr;
    Delegate *m_delegate = nullptr;
    std::vector<SessionObserver *> m_observers;
    std::set<std::string, std::less<>> m_presentedOutputs;
    SessionState m_state = SessionState::Active;
    SessionState m_unlockingFrom = SessionState::Locked;
};

} // namespace cutefish::session

#endif // CUTEFISH_SESSION_SESSION_CONTROLLER_H_
