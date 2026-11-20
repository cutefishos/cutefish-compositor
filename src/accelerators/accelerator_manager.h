// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/base/accelerators/accelerator_manager.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_MANAGER_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_MANAGER_H_

#include <list>
#include <vector>

#include "accelerators/accelerator.h"
#include "accelerators/accelerator_map.h"

namespace cutefish::accelerators {

class AcceleratorTarget
{
public:
    virtual ~AcceleratorTarget() = default;

    virtual bool acceleratorPressed(const Accelerator &accelerator) = 0;

    // acceleratorPressed() is only invoked on targets for which this is true.
    virtual bool canHandleAccelerators() const = 0;
};

// A target registered later has higher priority; a priority handler goes first.
class AcceleratorManager
{
public:
    enum HandlerPriority {
        NormalPriority,
        HighPriority,
    };

    AcceleratorManager();
    AcceleratorManager(const AcceleratorManager &) = delete;
    AcceleratorManager &operator=(const AcceleratorManager &) = delete;
    ~AcceleratorManager();

    void registerAccelerators(const std::vector<Accelerator> &accelerators,
                              HandlerPriority priority, AcceleratorTarget *target);
    void unregisterAccelerator(const Accelerator &accelerator, AcceleratorTarget *target);
    void unregisterAll(AcceleratorTarget *target);

    bool isRegistered(const Accelerator &accelerator) const;

    bool process(const Accelerator &accelerator);

    bool hasPriorityHandler(const Accelerator &accelerator) const;

    void setUsePositionalLookup(bool use) { m_accelerators.setUsePositionalLookup(use); }

private:
    class TargetInfo
    {
    public:
        void registerWithPriority(AcceleratorTarget *target, HandlerPriority priority);
        bool unregister(AcceleratorTarget *target);
        bool tryProcess(const Accelerator &accelerator);
        bool hasPriorityHandler() const;
        bool contains(AcceleratorTarget *target) const;

        bool hasTargets() const { return !m_targets.empty(); }

    private:
        std::list<AcceleratorTarget *> m_targets;
        bool m_hasPriorityHandler = false;
    };

    AcceleratorMap<TargetInfo> m_accelerators;
};

} // namespace cutefish::accelerators

#endif // CUTEFISH_ACCELERATORS_ACCELERATOR_MANAGER_H_
