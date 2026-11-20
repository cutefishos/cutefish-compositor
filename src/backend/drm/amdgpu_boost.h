// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_BACKEND_DRM_AMDGPU_BOOST_H_
#define CUTEFISH_BACKEND_DRM_AMDGPU_BOOST_H_

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include "backend/drm/gpu_boost_policy.h"
#include "base/timer.h"
#include "core/main_loop.h"

namespace cutefish::backend {

// Holds an amdgpu device at its peak stable pstate: under CPU load the SMU
// otherwise gives the power budget to the CPU and frames are missed.
class AmdgpuBoost
{
public:
    // Null when the device refuses a context.
    static std::unique_ptr<AmdgpuBoost> create(int fd);
    ~AmdgpuBoost();

    AmdgpuBoost(const AmdgpuBoost &) = delete;
    AmdgpuBoost &operator=(const AmdgpuBoost &) = delete;

    void compositorFrame();
    void expectAnimation();
    void setActive(bool active);

private:
    AmdgpuBoost(int fd, uint32_t context);

    void update(GpuBoostPolicy::Time now);
    void onReleaseTimeout();
    void want(bool peak);
    void run();

    const int m_fd;
    const uint32_t m_context;
    GpuBoostPolicy m_policy;
    bool m_wanted = false;
    base::Timer m_release {core::mainLoop()};

    // The pstate ioctl blocks for milliseconds, so it runs on a thread of its
    // own.
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_peak = false;
    bool m_quit = false;
    std::thread m_thread;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_AMDGPU_BOOST_H_
