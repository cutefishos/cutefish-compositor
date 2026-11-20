// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_QUIT_ALL_H_
#define CUTEFISH_WM_QUIT_ALL_H_

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "base/signal.h"
#include "base/timer.h"
#include "base/weak_ptr.h"

namespace cutefish::wm {

class Window;
class WindowManager;

class QuitAll
{
public:
    enum class Outcome {
        Quit,
        Cancelled,
        Unresponsive,
    };

    struct Result {
        Outcome outcome = Outcome::Quit;
        std::string applicationId;
    };

    static constexpr int kCancelledAfterMs = 2000;
    static constexpr int kUnresponsiveAfterMs = 10000;

    explicit QuitAll(WindowManager *manager, std::function<int64_t()> clock = {});

    // False while a request is already running.
    bool start(std::function<void(const Result &)> done);

    bool isRunning() const { return static_cast<bool>(m_done); }

    void update();

private:
    struct Asked {
        int64_t at = 0;
        std::optional<int64_t> promptSeen;
    };

    bool isApplicationWindow(const Window *window) const;
    void ask(Window *window, int64_t now);
    void finish(const Result &result);

    WindowManager *m_manager;
    std::function<int64_t()> m_clock;
    std::function<void(const Result &)> m_done;
    // By window id, which is never reused.
    std::map<uint64_t, Asked> m_asked;
    base::Timer m_timer;
    base::Subscription m_windowRemoved;
    base::WeakPtrFactory<QuitAll> m_weakFactory {this};
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_QUIT_ALL_H_
