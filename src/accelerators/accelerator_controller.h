// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/accelerators/accelerator_controller_impl.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

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

class AcceleratorController : public AcceleratorTarget
{

public:
    // `anyKeysPressed` feeds the launchpad state machine.
    AcceleratorController(AcceleratorConfiguration *configuration, const CommandContext &context,
                          std::function<bool()> anyKeysPressed);
    ~AcceleratorController() override;

    enum class Restriction {
        None,
        PreventProcessing,
        PreventProcessingAndPropagation,
    };

    // True once a target handled it.
    bool process(const Accelerator &accelerator);

    bool isReserved(const Accelerator &accelerator) const;

    bool isPreferred(const Accelerator &accelerator) const;

    bool isRepeatable(const Accelerator &accelerator) const;

    bool performActionIfEnabled(AcceleratorAction action, const Accelerator &accelerator);

    // Under the same restrictions as a key bound to `action`.
    bool performActionWithoutKey(AcceleratorAction action);

    bool doesAcceleratorMatchAction(const Accelerator &accelerator,
                                    AcceleratorAction action) const;

    // Punctuation keys are looked up by position on non-US layouts.
    void setUsePositionalLookup(bool use);

    // While set, nothing is processed.
    void setPreventProcessingAccelerators(bool prevent);
    bool shouldPreventProcessingAccelerators() const { return m_preventProcessing; }

    AcceleratorHistory *history() { return &m_history; }

    AcceleratorLaunchpadStateMachine *launchpadStateMachine() { return &m_launchpadStateMachine; }

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
