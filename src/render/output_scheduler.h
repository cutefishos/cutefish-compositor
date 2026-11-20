// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RENDER_OUTPUT_SCHEDULER_H_
#define CUTEFISH_RENDER_OUTPUT_SCHEDULER_H_

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>

namespace cutefish::render {

class OutputScheduler
{
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    using Duration = Clock::duration;

    // Timers wake in whole milliseconds, so a wakeup this close to the deadline
    // draws.
    static constexpr Duration kWakeupSlack = std::chrono::milliseconds(1);

    // amdgpu missed flips with up to 1.4 ms to spare.
    static constexpr Duration kLatchMargin = std::chrono::milliseconds(2);
    static constexpr size_t kDrawTimeWindow = 60;
    // So an output-wide animation after cheap frames is begun early enough.
    static constexpr Duration kFullRepaintMemory = std::chrono::seconds(10);

    enum class State {
        Idle,
        Scheduled,
        Rendering,
        // The next frame is rendered once the oldest lands.
        FlipPending,
        Suspended
    };
    enum class Reason {
        Startup,
        Resume,
        Scene,
        Surface,
        Decoration,
        Count
    };

    struct Stats {
        uint64_t requests = 0, coalesced = 0, failures = 0, staleCompletions = 0, skips = 0;
        uint64_t missed = 0;
        // Counted apart: their lost refresh is latency, not judder.
        uint64_t lateDemand = 0;
        std::array<uint64_t, static_cast<size_t>(Reason::Count)> reasons {};
    };

    OutputScheduler(Duration period, std::function<Time()> now = Clock::now);
    // `fullRepaint`: what is requested will redraw the whole output.
    void request(Reason reason, bool fullRepaint = false);
    void resume();
    void suspend();
    std::optional<Time> deadline() const;
    // Called only by the on-demand wakeup; early/spurious wakeups are harmless.
    uint64_t begin();
    bool submitted(uint64_t frame);
    // Frames land in submission order; `presented` is the display's stamp on
    // this clock, if any.
    bool complete(uint64_t frame, std::optional<Time> presented = std::nullopt);
    // Called before its complete().
    void drawn(uint64_t frame, Time done);
    void failed(uint64_t frame);
    // It answers the demand it was begun for; the next one waits for the
    // following slot.
    void skipped(uint64_t frame);

    // With two, the next frame renders during the previous one's flip; one is
    // double buffering.
    void setMaxPendingFrames(unsigned count);

    unsigned pendingFrames() const { return unsigned(m_pending.size()); }

    // The nominal slot start, never when the wakeup ran.
    Time frameTime() const { return m_frameTime; }

    // The slot's draw point; animations step on it rather than on the wall
    // clock.
    Time sampleTime() const { return m_sampleTime; }

    State state() const { return m_state; }

    const Stats& stats() const { return m_stats; }

    Duration period() const { return m_period; }

    // Also drops the vblank the slots were laid on.
    void setPeriod(Duration period)
    {
        m_period = period;
        m_drawTimes.clear();
        m_drawnBudget = m_lateBudget = period / 3;
        m_vblank.reset();
    }

    // Once flips carry timestamps.
    Duration drawBudget() const { return std::max(m_lateBudget, m_drawnBudget); }

    // Zero without one.
    Duration fullRepaintDrawTime() const;

    Duration slowestDrawTime() const
    {
        return m_drawTimes.empty() ? Duration::zero()
                                   : *std::max_element(m_drawTimes.begin(), m_drawTimes.end());
    }

private:
    struct Pending {
        uint64_t frame;
        Time aimed;
        // Cannot land before a refresh after the one ahead of it.
        bool queued = false;
        Time began {};
        // Begun by its draw point, so landing late means the budget is short.
        bool punctual = true;
        bool fullRepaint = false;
        Duration budget {};
        std::optional<Duration> took;
    };

    void schedule();
    void settle();
    Time drawPoint(Time slot) const;
    Duration budget() const;
    Time slotAt(Time time) const;
    bool behindPendingFrame(Time now) const;
    bool throttled() const { return m_pending.size() >= m_maxPending; }
    Duration m_period;
    std::function<Time()> m_now;
    State m_state = State::Suspended;
    Time m_next {}, m_deadline {}, m_frameTime {}, m_sampleTime {}, m_began {};
    // Unset without timestamps.
    std::optional<Time> m_vblank;
    bool m_punctual = true;
    Duration m_frameBudget {};
    Duration m_lateBudget;
    // Drops as soon as that frame leaves the window.
    Duration m_drawnBudget;
    std::deque<Duration> m_drawTimes;
    std::deque<std::pair<Time, Duration>> m_fullDrawTimes;
    bool m_fullDemand = false;
    bool m_fullFrame = false;
    uint64_t m_requested = 0, m_consumed = 0, m_snapshot = 0;
    uint64_t m_serial = 0, m_frame = 0;
    std::deque<Pending> m_pending;
    std::optional<Time> m_landed;
    unsigned m_maxPending = 1;
    unsigned m_failures = 0;
    Stats m_stats;
};
} // namespace cutefish::render
#endif
