// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/drm/amdgpu_boost.h"

#include <amdgpu_drm.h>
#include <xf86drm.h>

namespace cutefish::backend {

namespace {

int contextOp(int fd, uint32_t op, uint32_t context, uint32_t flags, uint32_t *allocated = nullptr)
{
    drm_amdgpu_ctx args = {};
    args.in.op = op;
    args.in.ctx_id = context;
    args.in.flags = flags;
    const int result = drmCommandWriteRead(fd, DRM_AMDGPU_CTX, &args, sizeof(args));
    if (result == 0 && allocated)
        *allocated = args.out.alloc.ctx_id;
    return result;
}

} // namespace

std::unique_ptr<AmdgpuBoost> AmdgpuBoost::create(int fd)
{
    uint32_t context = 0;
    if (contextOp(fd, AMDGPU_CTX_OP_ALLOC_CTX, 0, 0, &context) != 0)
        return nullptr;
    return std::unique_ptr<AmdgpuBoost>(new AmdgpuBoost(fd, context));
}

AmdgpuBoost::AmdgpuBoost(int fd, uint32_t context)
    : m_fd(fd)
    , m_context(context)
{
    m_release.setSingleShot(true);
    m_release.onTimeout = [this] { onReleaseTimeout(); };
    m_thread = std::thread([this] { run(); });
}

AmdgpuBoost::~AmdgpuBoost()
{
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_changed.notify_one();
    m_thread.join();
    // Freeing the context gives back the level it held.
    contextOp(m_fd, AMDGPU_CTX_OP_FREE_CTX, m_context, 0);
}

void AmdgpuBoost::compositorFrame()
{
    const auto now = GpuBoostPolicy::Clock::now();
    m_policy.compositorFrame(now);
    update(now);
}

void AmdgpuBoost::expectAnimation()
{
    const auto now = GpuBoostPolicy::Clock::now();
    m_policy.expectAnimation(now);
    update(now);
}

void AmdgpuBoost::update(GpuBoostPolicy::Time now)
{
    if (!m_policy.holds(now))
        return;
    want(true);
    if (!m_release.isActive())
        m_release.start(*m_policy.releaseTime() - now);
}

void AmdgpuBoost::setActive(bool active)
{
    m_policy.setActive(active);
    if (!active) {
        m_release.stop();
        want(false);
    }
}

void AmdgpuBoost::onReleaseTimeout()
{
    const auto now = GpuBoostPolicy::Clock::now();
    if (m_policy.holds(now))
        m_release.start(*m_policy.releaseTime() - now);
    else
        want(false);
}

void AmdgpuBoost::want(bool peak)
{
    if (peak == m_wanted)
        return;
    m_wanted = peak;
    {
        std::lock_guard lock(m_mutex);
        m_peak = peak;
    }
    m_changed.notify_one();
}

void AmdgpuBoost::run()
{
    bool applied = false;
    std::unique_lock lock(m_mutex);
    while (true) {
        m_changed.wait(lock, [&] { return m_quit || m_peak != applied; });
        if (m_quit)
            return;
        applied = m_peak;
        lock.unlock();
        // EBUSY while another context holds a pstate, EINVAL on a kernel
        // without the operation: frames then go on at the clock they get.
        contextOp(m_fd, AMDGPU_CTX_OP_SET_STABLE_PSTATE, m_context,
                  applied ? AMDGPU_CTX_STABLE_PSTATE_PEAK : AMDGPU_CTX_STABLE_PSTATE_NONE);
        lock.lock();
    }
}

} // namespace cutefish::backend
