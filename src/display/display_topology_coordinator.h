// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "display/callback_native_display_delegate.h"
#include "base/signal.h"
#include "base/timer.h"
#include "display/display_configuration_controller.h"
#include <functional>
#include <memory>

namespace cutefish::display {

class DisplayTopologyCoordinator
{
public:
    using Probe = std::function<std::optional<HardwareSnapshot>()>;
    DisplayTopologyCoordinator(DisplayManager *manager, CallbackNativeDisplayDelegate *delegate,
                               DisplayConfigurationController *controller, Probe probe,
                               DisplayPreferences preferences = DisplayPreferences());
    DisplayTopologyCoordinator(const DisplayTopologyCoordinator &) = delete;
    DisplayTopologyCoordinator &operator=(const DisplayTopologyCoordinator &) = delete;
    void invalidate();
    // Unlike invalidate(), the startup layout is not carried over: unsaved
    // displays get defaults.
    void start();

    base::Signal<bool> reconciled;

private:
    void schedule();
    void run();
    DisplayConfiguration candidate(const HardwareSnapshot &hardware, uint64_t revision) const;
    DisplayManager *m_manager;
    CallbackNativeDisplayDelegate *m_delegate;
    DisplayConfigurationController *m_controller;
    Probe m_probe;
    DisplayPreferences m_preferences;
    base::Timer m_retry;
    base::Subscription m_operationFinished;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::optional<AppliedConfiguration> m_previous;
    bool m_pending = false;
    bool m_running = false;
    std::string m_operationToken;
    int m_attempts = 0;
    uint64_t m_revision = 0;
};

} // namespace cutefish::display
