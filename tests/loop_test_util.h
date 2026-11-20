// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <chrono>
#include <functional>
#include <optional>

// Kept for tests that still reach these through this header.
#include "base/event_loop.h"
#include "base/timer.h"
#include "core/main_loop.h"

namespace cutefish::test {

// Runs the main loop until done() holds or the timeout passes; returns done().
bool runUntil(const std::function<bool()>& done,
              std::chrono::milliseconds timeout = std::chrono::seconds(5));

void runFor(std::chrono::milliseconds duration);

// Runs whatever is ready on the main loop once, without waiting.
void processPendingEvents();

} // namespace cutefish::test
