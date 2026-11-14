// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/accelerators/accelerator_controller_impl.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65
//
// Adapted for Cutefish: the restriction sets that have a Cutefish state
// behind them (blocked session, no window), the launchpad
// state machine, the repeatable actions. Tablet volume, caps lock, suspend and
// IME state machines, the exit warning, deprecated accelerators, enterprise
// policy and metrics are dropped.

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_CONTROLLER_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_CONTROLLER_H_

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "accelerators/accelerator.h"
#include "accelerators/accelerator_actions.h"
#include "accelerators/accelerator_commands.h"
#include "accelerators/accelerator_launchpad_state_machine.h"
#include "accelerators/accelerator_manager.h"
#include "accelerators/accelerator_history.h"
#include "base/signal.h"

namespace cutefish::accelerators {

class AcceleratorConfiguration;

// Registers the configured accelerators with an AcceleratorManager and is the
// target that performs them.
class AcceleratorController : public AcceleratorTarget
{

public:
    // `anyKeysPressed` feeds the launchpad state machine.
    AcceleratorController(AcceleratorConfiguration *configuration, const CommandContext &context,
                          std::function<bool()> anyKeysPressed);
    ~AcceleratorController() override;

    // A list of possible ways in which an accelerator should be restricted
    // before processing.
    enum class Restriction {
        None,
        PreventProcessing,
        // Don't process the accelerator and prevent propagation to the
        // focused window.
        PreventProcessingAndPropagation,
    };

    // Activates the target for the accelerator; true once one handled it.
    bool process(const Accelerator &accelerator);

    // A reserved accelerator is always handled and never passed to a window.
    bool isReserved(const Accelerator &accelerator) const;

    // A preferred accelerator is handled before being passed to a window,
    // unless the window is fullscreen.
    bool isPreferred(const Accelerator &accelerator) const;

    // A repeatable accelerator is performed again while its key is held.
    bool isRepeatable(const Accelerator &accelerator) const;

    bool performActionIfEnabled(AcceleratorAction action, const Accelerator &accelerator);

    // Runs `action` as a key bound to it would be, under the same restrictions,
    // for a trigger that is not a key: a hot corner.
    bool performActionWithoutKey(AcceleratorAction action);

    bool doesAcceleratorMatchAction(const Accelerator &accelerator,
                                    AcceleratorAction action) const;

    // Ash's InputMethodChanged: both the configuration and the manager look
    // punctuation keys up by position on non-US layouts.
    void setUsePositionalLookup(bool use);

    // While set, nothing is processed: the Settings shortcut recorder uses this
    // so that pressing Meta while recording does not open the launchpad.
    void setPreventProcessingAccelerators(bool prevent);
    bool shouldPreventProcessingAccelerators() const { return m_preventProcessing; }

    AcceleratorHistory *history() { return &m_history; }

    AcceleratorLaunchpadStateMachine *launchpadStateMachine() { return &m_launchpadStateMachine; }

    // AcceleratorTarget:
    bool acceleratorPressed(const Accelerator &accelerator) override;
    bool canHandleAccelerators() const override { return true; }

private:
    void init();
    void registerAccelerators(std::vector<Accelerator> accelerators);
    void onAcceleratorsUpdated();

    bool canPerformAction(AcceleratorAction action, const Accelerator &accelerator) const;
    void performAction(AcceleratorAction action, const Accelerator &accelerator);
    Restriction acceleratorProcessingRestriction(AcceleratorAction action) const;

    AcceleratorManager m_manager;
    AcceleratorHistory m_history;
    AcceleratorLaunchpadStateMachine m_launchpadStateMachine;
    AcceleratorConfiguration *m_configuration = nullptr;
    CommandContext m_context;

    std::set<AcceleratorAction> m_actionsAllowedAtLockScreen;
    std::set<AcceleratorAction> m_preferredActions;
    std::set<AcceleratorAction> m_reservedActions;
    std::set<AcceleratorAction> m_actionsNeedingWindow;
    std::set<AcceleratorAction> m_actionsBlockedInOverview;
    std::set<AcceleratorAction> m_repeatableActions;

    bool m_preventProcessing = false;
    base::Subscription m_configurationUpdates;
};

} // namespace cutefish::accelerators

#endif // CUTEFISH_ACCELERATORS_ACCELERATOR_CONTROLLER_H_
