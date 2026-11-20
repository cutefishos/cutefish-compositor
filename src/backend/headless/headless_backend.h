// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_BACKEND_HEADLESS_BACKEND_H_
#define CUTEFISH_BACKEND_HEADLESS_BACKEND_H_

#include <memory>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "base/timer.h"

namespace cutefish {

namespace compositor {
class Compositor;
struct Output;
} // namespace compositor

namespace backend {

class HeadlessBackend
{
public:
    static base::Size outputSize() { return base::Size(1920, 1080); }

    HeadlessBackend(compositor::Compositor *compositor, int outputCount);
    HeadlessBackend(const HeadlessBackend &) = delete;
    HeadlessBackend &operator=(const HeadlessBackend &) = delete;

    // A toolkit client with no output has nowhere to place itself.
    std::vector<std::unique_ptr<compositor::Output>> createOutputs(compositor::Output *primary);

    // A client that never gets a frame callback stops drawing, so a 60 Hz timer
    // stands in for the flip.
    void startClock();

    // Emitted before the compositor is told, so session state settles first.
    base::Signal<> aboutToPresent;

private:
    compositor::Compositor *const m_compositor;
    const int m_outputCount;
    base::Timer m_clock;
    uint64_t m_generation = 0;
};

} // namespace backend
} // namespace cutefish

#endif // CUTEFISH_BACKEND_HEADLESS_BACKEND_H_
