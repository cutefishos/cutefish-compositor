// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_CORE_MAIN_LOOP_H_
#define CUTEFISH_CORE_MAIN_LOOP_H_

#include <memory>

namespace cutefish::base {
class EventLoop;
} // namespace cutefish::base

namespace cutefish::core {

// Made before anything that uses it and destroyed after. A second one stands in
// for the first while it exists.
class MainLoop
{
public:
    MainLoop();
    ~MainLoop();

    MainLoop(const MainLoop &) = delete;
    MainLoop &operator=(const MainLoop &) = delete;

    base::EventLoop &loop() const { return *m_loop; }

private:
    std::unique_ptr<base::EventLoop> m_loop;
    MainLoop *m_previous = nullptr;
};

// Null when no MainLoop exists.
base::EventLoop *mainLoop();

} // namespace cutefish::core

#endif // CUTEFISH_CORE_MAIN_LOOP_H_
