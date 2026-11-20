// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/accelerators/accelerator_history_impl.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_HISTORY_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_HISTORY_H_

#include <set>

#include "accelerators/accelerator.h"

namespace cutefish::accelerators {

class AcceleratorHistory
{
public:
    AcceleratorHistory();
    AcceleratorHistory(const AcceleratorHistory &) = delete;
    AcceleratorHistory &operator=(const AcceleratorHistory &) = delete;
    ~AcceleratorHistory();

    const Accelerator &currentAccelerator() const { return m_currentAccelerator; }

    const Accelerator &previousAccelerator() const { return m_previousAccelerator; }

    const std::set<uint32_t> &currentlyPressedKeys() const { return m_currentlyPressedKeys; }

    // Stores `accelerator` if it differs from the current one.
    void storeCurrentAccelerator(const Accelerator &accelerator);

private:
    Accelerator m_currentAccelerator;
    Accelerator m_previousAccelerator;
    std::set<uint32_t> m_currentlyPressedKeys;
};

} // namespace cutefish::accelerators

#endif // CUTEFISH_ACCELERATORS_ACCELERATOR_HISTORY_H_
