// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/drm/drm_output.h"

#include <drm_fourcc.h>
#include <xf86drm.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "backend/drm/drm_backend.h"
#include "backend/drm/drm_cursor.h"
#include "backend/drm/drm_device_api.h"
#include "base/auto_reset.h"
#include "base/event_loop.h"
#include "base/strings.h"
#include "core/logging.h"
#include "render/egl_renderer.h"
#include "scene/surface_node.h"

namespace cutefish::backend {

namespace {

constexpr uint64_t kCursorEventFlag = 1ull << 63;

} // namespace

void DrmOutput::setRenderCallback(std::function<void(render::Renderer &, uint64_t)> callback)
{
    m_renderCallback = std::move(callback);
}

void DrmOutput::setPrepareCallback(std::function<void(render::Renderer &, uint64_t)> callback)
{
    m_prepareCallback = std::move(callback);
}

void DrmOutput::setBeginFrameCallback(std::function<bool(double)> callback)
{
    m_beginFrameCallback = std::move(callback);
}

void DrmOutput::setScanoutPlanner(std::function<scene::OverlayPlan(const base::Size &, size_t)> planner)
{
    m_scanoutPlanner = std::move(planner);
}

std::unique_ptr<render::Texture>
DrmOutput::renderOffscreen(const render::OutputRenderContext &context,
                           const std::function<void(render::Renderer &)> &paint)
{
    if (!m_enabled || m_blanked || m_retiring || !m_renderer || !m_target)
        return {};
    m_renderer->setOutputContext(context);
    if (!m_renderer->beginFrame(m_target.get()))
        return {};
    std::unique_ptr<render::Texture> texture;
    const bool rendered = m_renderer->renderToTexture(texture, context.logicalViewport, [&] {
        base::AutoReset<bool> capturing(m_renderer->capturing, true);
        paint(*m_renderer);
    });
    m_renderer->cancelFrame();
    if (!rendered)
        texture.reset();
    return texture;
}

void DrmOutput::retire(SubmittedFrame &frame, const render::SyncFence &fence)
{
    for (PlaneContent &content : frame.planes) {
        if (content.buffer)
            content.buffer->onScanoutReleased(fence.duplicate());
    }
    frame.composited.reset();
    frame = SubmittedFrame();
}

void DrmOutput::releaseScanoutBuffers()
{
    if (m_queuedFrame) {
        retire(m_queuedFrame->submitted, render::SyncFence());
        m_queuedFrame.reset();
    }
    retire(m_pendingFrame, render::SyncFence());
    retire(m_scanoutFrame, render::SyncFence());
}

std::vector<uint64_t> DrmOutput::dropOutstandingFrames()
{
    if (m_queuedFrame) {
        retire(m_queuedFrame->submitted, render::SyncFence());
        m_queuedFrame.reset();
    }
    return std::exchange(m_outstanding, {});
}

void DrmOutput::commitQueued()
{
    if (!m_queuedFrame)
        return;
    QueuedFrame queued = std::move(*m_queuedFrame);
    m_queuedFrame.reset();
    if (!m_enabled || m_blanked || !commit(queued.submitted.planes, false, queued.frame)) {
        retire(queued.submitted, render::SyncFence());
        std::erase(m_outstanding, queued.frame);
        frameDiscarded(queued.frame);
        m_scheduler.failed(queued.frame);
        return;
    }
    m_presentations.submitted(queued.frame, queued.submitted.presentation);
    queued.submitted.releaseFence = std::move(m_lastReleaseFence);
    retire(m_pendingFrame, render::SyncFence());
    m_pendingFrame = std::move(queued.submitted);
    m_bufferFrame = queued.frame;
}

void DrmOutput::clearPromotions()
{
    for (scene::SurfaceNode *node : m_promoted)
        node->setPromotedToOverlay(false);
    m_promoted.clear();
}

void DrmOutput::scheduleFrame(Reason reason)
{
    if (m_retiring || m_blanked)
        return;
    if (reason == Reason::Startup || reason == Reason::Resume)
        m_forceFrame = true;
    m_scheduler.request(reason, m_fullRepaintQuery && m_fullRepaintQuery());
    armWakeup();
}

void DrmOutput::warmUp()
{
    if (m_retiring || m_blanked)
        return;
    m_forceFrame = true;
    scheduleFrame(Reason::Scene);
}

void DrmOutput::armWakeup()
{
    const auto deadline = m_scheduler.deadline();
    if (!deadline) {
        m_wakeup.stop();
        return;
    }
    // Damage cannot push a deadline back; a flip that moved the slots can
    // bring it forward.
    if (m_wakeup.isActive() && *deadline >= m_armedDeadline)
        return;
    m_armedDeadline = *deadline;
    // Rounded down: OutputScheduler::begin() takes a wakeup within its slack
    // of the deadline, where rounding up would start the frame late.
    const auto remaining = *deadline - m_now();
    const auto ms = std::chrono::floor<std::chrono::milliseconds>(remaining).count();
    m_wakeup.start(std::chrono::milliseconds(std::max<int64_t>(0, ms)));
}

uint64_t DrmOutput::aimedNs() const
{
    const auto aimed = m_scheduler.frameTime() + m_scheduler.period();
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(aimed.time_since_epoch())
                           .count());
}

void DrmOutput::wakeup()
{
    const auto frame = m_scheduler.begin();
    const bool compositorChange = frame && std::exchange(m_compositorChange, false);
    const bool drawn = frame
            && (!m_beginFrameCallback
                || m_beginFrameCallback(std::chrono::duration<double, std::milli>(
                                                m_scheduler.sampleTime().time_since_epoch())
                                                .count())
                || m_forceFrame);
    if (frame && !drawn) {
        m_scheduler.skipped(frame);
    } else if (frame) {
        const auto started = m_now();
        if (renderFrame(frame, false)) {
            m_forceFrame = false;
            m_scheduler.submitted(frame);
            frameDrawn(frame, aimedNs());
            if (compositorChange && compositorFrameDrawn)
                compositorFrameDrawn();
            const auto took = m_now() - started;
            if (took > m_scheduler.period()) {
                CF_DEBUG(cfDrm) << "Composited frame" << frame << "for" << m_config.name << "in"
                                << std::chrono::duration<double, std::milli>(took).count()
                                << "ms, longer than its refresh period";
            }
        } else {
            std::erase(m_outstanding, frame);
            frameDiscarded(frame);
            m_scheduler.failed(frame);
        }
    }
    armWakeup();
    scheduleCursorCommit();
}

bool DrmOutput::renderFrame(uint64_t frame, bool modeset)
{
    bool prepared = false;
    if (renderAttempt(frame, modeset, m_directScanoutEnabled, prepared))
        return true;
    if (!m_directScanoutEnabled || !m_enabled)
        return false;

    // A configuration that passed TEST_ONLY can still fail the real commit; the
    // frame is then composited.
    CF_WARNING(cfDrm) << "Hardware plane commit failed; compositing this frame instead";
    return renderAttempt(frame, modeset, false, prepared, true);
}

bool DrmOutput::renderAttempt(uint64_t frame, bool modeset, bool allowPromotion, bool &prepared,
                              bool retry)
{
    if (!m_enabled || m_blanked || !m_renderer->beginFrame(m_target.get()))
        return false;

    render::PresentationSubmission presentation;

    struct ResetCapture {
        render::Renderer *renderer;

        ~ResetCapture()
        {
            renderer->presentationSubmission = nullptr;
            renderer->frameTime = -1;
            renderer->framePeriod = 0;
        }
    } resetCapture {m_renderer};

    m_renderer->presentationSubmission = &presentation;
    m_renderer->frameTime = std::chrono::duration<double, std::milli>(
                                    m_scheduler.sampleTime().time_since_epoch())
                                    .count();
    m_renderer->framePeriod =
            std::chrono::duration<double, std::milli>(m_scheduler.period()).count();
    ++m_frameCount;
    if (std::find(m_outstanding.begin(), m_outstanding.end(), frame) == m_outstanding.end())
        m_outstanding.push_back(frame);
    if (!prepared && m_prepareCallback)
        m_prepareCallback(*m_renderer, frame);
    prepared = true;

    // The primary plane's current framebuffer stands in for this frame's: same
    // size, format and modifier.
    PlaneContent composited;
    if (!m_scanoutFrame.planes.empty() && !m_scanoutFrame.planes.front().isDirectScanout())
        composited = m_scanoutFrame.planes.front();

    scene::OverlayPlan plan;
    std::vector<PlaneContent> planes;
    // A plane the blend cannot reach would show untinted; the cursor's image
    // carries the tint instead.
    const bool blended = blendsColorTransform();
    if (m_cursor)
        m_cursor->setTint(blended ? m_colorTransform : render::ColorTransform());
    if (allowPromotion && !blended && !m_secureScanout && !modeset && m_scanout
        && m_scanoutPlanner) {
        plan = m_scanoutPlanner(size(), m_scanout->maxOverlays());
        planes = m_scanout->validate(plan, composited, m_modeBlobId);
    }

    const bool direct = !planes.empty() && planes.front().isDirectScanout();
    for (PlaneContent &content : planes) {
        if (!content.node)
            continue;
        content.node->setPromotedToOverlay(true);
        m_promoted.push_back(content.node);
        presentation.add(content.node->presentationContent(), true);
    }

    std::unique_ptr<CompositedBuffer> compositedBuffer;
    render::GpuTimer::Mark drawnMark;
    render::SyncFence drawnFence;
    if (direct) {
        m_renderer->cancelFrame();
    } else {
        // Plane promotion changes pixels without changing scene revisions.
        if (retry || planes.size() > 1 || m_scanoutFrame.planes.size() > 1) {
            const auto viewport = m_renderer->outputContext().logicalViewport;
            m_renderer->prepareDamage(viewport.isEmpty() ? base::Rect(size()) : viewport,
                                      {});
        }
        // A buffer kept from an earlier frame holds another tint, or none.
        if (blended || m_untintedRepaints > 0)
            m_renderer->invalidateFrame();
        if (m_renderCallback)
            m_renderCallback(*m_renderer, frame);
        // After the scene and the captures that copy it, which stay untinted.
        if (blended)
            m_renderer->tintFrame(m_colorTransform);
        if (m_renderer->marksDrawn())
            drawnMark = m_renderer->markDrawn();
        else
            drawnFence = m_renderer->createReleaseFence();
        if (!m_enabled || !m_renderer->endFrame()) {
            clearPromotions();
            return false;
        }
        if (blended)
            m_untintedRepaints = 0;
        else if (m_untintedRepaints > 0)
            --m_untintedRepaints;
        compositedBuffer = m_acquireBuffer();
        if (!compositedBuffer) {
            CF_WARNING(cfDrm) << "Failed to obtain a framebuffer for the composited frame";
            clearPromotions();
            return false;
        }
        PlaneContent primary = compositedBuffer->primary;
        primary.planeId = planes.empty() ? m_config.planeId : planes.front().planeId;
        primary.displayRect = base::Rect(size());
        if (planes.empty())
            planes.push_back(std::move(primary));
        else
            planes.front() = std::move(primary);
    }

    // A blocking modeset can still have an event to deliver, but it has
    // finished using the commit slot.
    const bool cursorFlipPending = !m_events.empty() && (m_events.rbegin()->first & kCursorEventFlag);
    const bool queue = !modeset && (m_bufferFrame != 0 || cursorFlipPending);
    if (!queue && !commit(planes, modeset, frame)) {
        clearPromotions();
        return false;
    }

    if (direct)
        ++m_directScanoutFrames;
    if (planes.size() > 1)
        ++m_overlayFrames;
    if (presentationSubmitted)
        presentationSubmitted(presentation);
    SubmittedFrame submitted;
    submitted.presentation = std::move(presentation);
    if (!queue)
        m_presentations.submitted(frame, submitted.presentation);
    submitted.planes = std::move(planes);
    submitted.composited = std::move(compositedBuffer);
    submitted.drawnMark = std::move(drawnMark);
    submitted.drawnFence = std::move(drawnFence);
    if (!queue)
        submitted.releaseFence = std::move(m_lastReleaseFence);
    submitted.secure = m_secureScanout && isSecureConfiguration(submitted.planes)
            && (!m_secureContentCheck || m_secureContentCheck(m_renderer->frameTime));
    for (PlaneContent &content : submitted.planes) {
        if (content.buffer)
            content.buffer->onScanoutAcquired();
    }

    if (queue) {
        if (m_queuedFrame) {
            const uint64_t stale = m_queuedFrame->frame;
            retire(m_queuedFrame->submitted, render::SyncFence());
            m_queuedFrame.reset();
            frameDiscarded(stale);
            m_scheduler.failed(stale);
        }
        m_queuedFrame = QueuedFrame {frame, std::move(submitted)};
    } else if (modeset) {
        // A blocking modeset has already taken effect, so the previous frame is
        // done being read the moment it returns.
        releaseScanoutBuffers();
        m_scanoutFrame = std::move(submitted);
        m_lastPresentedSecure = m_scanoutFrame.secure;
        m_bufferFrame = 0;
    } else {
        retire(m_pendingFrame, render::SyncFence());
        m_pendingFrame = std::move(submitted);
        m_bufferFrame = frame;
    }

    clearPromotions();
    return true;
}

void DrmOutput::dispatchPageFlip(FlipEvent event, uint32_t seconds, uint32_t microseconds,
                                 uint32_t sequence)
{
    if (event.output)
        event.output->onPageFlip(event.frame, seconds, microseconds, sequence);
}

void DrmOutput::onPageFlip(uint64_t frame, uint32_t seconds, uint32_t microseconds,
                           uint32_t sequence)
{
    if (!m_events.erase(frame))
        return; // One unique kernel event per token.
    if (frame & kCursorEventFlag) {
        finishCursorFlip();
        return;
    }
    // virtio-gpu stamps every flip with sequence 0 under a made-up refresh, so
    // such an output is paced at 60 Hz.
    if (!m_unsynced && sequence == 0 && m_lastSequence == 0 && m_completions > 0
        && !m_device->flipTimesMarkRefresh()) {
        m_unsynced = true;
        m_scheduler.setPeriod(framePeriod(pacingRate()));
        CF_INFO(cfDrm) << m_config.name << "has no vblank; pacing at" << pacingRate()
                       << "Hz instead of the mode's" << refreshRate();
    }
    m_presentations.complete(frame, seconds, microseconds, sequence, m_monotonicClock,
                             pacingRate(), presentationOutput);
    const uint32_t timeMs = seconds * 1000u + microseconds / 1000u;
    ++m_completions;
    m_lastSequence = sequence;
    m_lastTimeMs = timeMs;
    // std::steady_clock is CLOCK_MONOTONIC on Linux, the clock a monotonic
    // DRM device stamps its events with.
    std::optional<render::OutputScheduler::Time> presented;
    if (m_monotonicClock && microseconds < 1000000) {
        presented = render::OutputScheduler::Time(std::chrono::seconds(seconds)
                                                  + std::chrono::microseconds(microseconds));
    }
    finishFlip(frame, timeMs, presented);
}

void DrmOutput::finishFlip(uint64_t frame, uint32_t timeMs,
                           std::optional<render::OutputScheduler::Time> presented)
{
    if (m_events.empty())
        m_flipTimeout.stop();
    else
        m_flipTimeout.start();
    // Before commitQueued() puts the next flip in flight.
    if (m_cursor && m_events.empty())
        m_cursor->setFlipPending(false);
    if (frame == m_bufferFrame && !m_pendingFrame.planes.empty()) {
        if (const auto done = m_renderer ? m_renderer->drawnAt(m_pendingFrame.drawnMark)
                                         : std::nullopt) {
            m_scheduler.drawn(frame, *done);
        } else if (const auto done = m_pendingFrame.drawnFence.signalTimeNs()) {
            m_scheduler.drawn(frame,
                              render::OutputScheduler::Time(std::chrono::nanoseconds(*done)));
        }
        m_pendingFrame.drawnMark = {};
        m_pendingFrame.drawnFence.reset();
        // The commit that just landed is what stopped the previous frame's
        // buffers being read, so its out-fence is their release fence.
        retire(m_scanoutFrame, m_pendingFrame.releaseFence);
        m_scanoutFrame = std::move(m_pendingFrame);
        m_lastPresentedSecure = m_scanoutFrame.secure;
        m_pendingFrame = SubmittedFrame();
        m_bufferFrame = 0;
        // Before anyone hears of this completion: the next frame is already
        // drawn, and every microsecond it waits here is latency.
        commitQueued();
    }
    // A device without vblank stamps when the host finished, which is no
    // refresh to lay the slots on.
    if (m_scheduler.complete(frame, m_unsynced ? std::nullopt : presented)) {
        // A client may request its next frame only after this one presents.
        // Leave the KMS slot free for a refresh before treating the output as idle.
        m_cursorOnlyAfter = m_now() + m_scheduler.period();
        std::erase(m_outstanding, frame);
        imagePresented(frame,
                              presented ? uint64_t(std::chrono::nanoseconds(
                                                           presented->time_since_epoch())
                                                           .count())
                                        : 0);
        framePresented(timeMs, frame);
    }
    armWakeup();
    scheduleCursorCommit();
}

void DrmOutput::onFlipTimeout()
{
    if (m_events.empty())
        return;
    const auto oldest = m_events.begin();
    const uint64_t frame = oldest->first;
    CF_WARNING(cfDrm) << "No page flip event on" << m_config.name << "for frame" << frame
                      << "within"
                      << std::chrono::duration_cast<std::chrono::milliseconds>(m_flipTimeout.interval())
                                .count()
                     << "ms; no longer waiting for it";
    retireEvent(std::move(oldest->second));
    m_events.erase(oldest);
    if (frame & kCursorEventFlag) {
        finishCursorFlip();
        return;
    }
    m_presentations.abandon(frame);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            m_now().time_since_epoch());
    finishFlip(frame, uint32_t(now.count()), std::nullopt);
}

void DrmOutput::retireEvent(std::unique_ptr<FlipEvent> event)
{
    event->output = nullptr;
    if (m_backend)
        m_backend->retainFlipEvent(std::move(event));
    else if (m_retireEvent)
        m_retireEvent(std::move(event));
}

void DrmOutput::reportStats() const
{
    CF_INFO(base::LogCategory::defaultCategory()) << statsLine();
}

std::string DrmOutput::statsLine() const
{
    const auto &s = m_scheduler.stats();
    const auto ms = [](std::chrono::steady_clock::duration duration) {
        return std::chrono::duration<double, std::milli>(duration).count();
    };
    std::string line;
    const auto add = [&line](const auto &value) {
        if (!line.empty())
            line += ' ';
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, bool>)
            line += value ? "true" : "false";
        else if constexpr (std::is_floating_point_v<Value>)
            line += base::strings::number(double(value));
        else if constexpr (std::is_arithmetic_v<Value>)
            line += std::to_string(value);
        else
            line += value;
    };
    add("Output scheduling");
    add('"' + m_config.name + '"');
    add("duration-ms");
    add(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()
                                                              - m_statsStart)
                .count());
    add("process-CPU-ms");
    add(1000.0 * (std::clock() - m_cpuStart) / CLOCKS_PER_SEC);
    add("refresh-Hz");
    add(refreshRate());
    add("pacing-Hz");
    add(pacingRate());
    add("period-ns");
    add(std::chrono::duration_cast<std::chrono::nanoseconds>(m_scheduler.period()).count());
    for (const auto &[label, value] : {std::pair<const char *, uint64_t> {"renders", m_frameCount},
                                       {"KMS-commits", m_commits},
                                       {"normal-commits", m_flipCommits},
                                       {"unique-completions", m_completions},
                                       {"missed-slots", s.missed},
                                       {"late-demand", s.lateDemand},
                                       {"requests", s.requests},
                                       {"coalesced", s.coalesced},
                                       {"failures", s.failures},
                                       {"stale-completions", s.staleCompletions},
                                       {"skipped", s.skips},
                                       {"direct-scanout-frames", m_directScanoutFrames},
                                       {"overlay-frames", m_overlayFrames},
                                       {"plane-test-failures",
                                        m_scanout ? m_scanout->testFailures() : 0},
                                       {"scanout-import-failures",
                                        m_scanout ? m_scanout->importFailures() : 0}}) {
        add(label);
        add(value);
    }
    add("hardware-cursor");
    add(m_cursor != nullptr);
    add("last-kernel-sequence");
    add(m_lastSequence);
    add("last-kernel-time-ms");
    add(m_lastTimeMs);
    add("draw-budget-ms");
    add(ms(m_scheduler.drawBudget()));
    add("slowest-draw-ms");
    add(ms(m_scheduler.slowestDrawTime()));
    add("reasons startup/resume/scene/surface/decoration");
    for (int i = 0; i < 5; ++i)
        add(s.reasons[i]);
    return line;
}

bool DrmOutput::commit(const std::vector<PlaneContent> &planes, bool modeset, uint64_t frame)
{
    if (!m_scanout)
        return false;

    FlipEvent *token = nullptr;
    if (frame) {
        auto event = std::make_unique<FlipEvent>(FlipEvent {weakPtr(), frame});
        token = event.get();
        m_events.emplace(frame, std::move(event));
    }

    std::optional<PlaneContent> cursorContent;
    if (m_cursor && m_cursor->usesAtomicPlane())
        cursorContent = m_cursor->atomicPlaneContent();

    render::SyncFence releaseFence;
    const bool ok = m_scanout->commit(planes, modeset, token, m_modeBlobId,
                                      modeset ? nullptr : &releaseFence, m_secureScanout,
                                      cursorContent ? &*cursorContent : nullptr);
    if (!ok) {
        const int error = errno;
        if (frame)
            m_events.erase(frame);
        CF_WARNING(cfDrm).printf("Atomic commit failed (%s): %s", modeset ? "modeset" : "flip",
                                 strerror(error));
        return false;
    }

    if (cursorContent && m_cursor) {
        m_cursor->markAtomicCommitted(!m_secureScanout);
        m_cursorCommitTimer.stop();
    }
    if (frame && m_cursor)
        m_cursor->setFlipPending(true);
    if (frame && !m_flipTimeout.isActive())
        m_flipTimeout.start();
    if (modeset)
        m_presentations.discontinuity();
    m_lastReleaseFence = std::move(releaseFence);
    ++m_commits;
    if (!modeset)
        ++m_flipCommits;
    return true;
}

bool DrmOutput::canCommitCursorOnly() const
{
    return m_enabled && !m_blanked && !m_retiring && !m_configuring && !m_secureScanout
            && m_sessionActive && m_sessionActive() && m_scanout && m_cursor
            && m_cursor->usesAtomicPlane() && m_cursor->hasPendingAtomicUpdate()
            && m_scheduler.state() == render::OutputScheduler::State::Idle
            && m_events.empty() && !m_bufferFrame && !m_queuedFrame;
}

void DrmOutput::scheduleCursorCommit()
{
    if (m_cursorCommitScheduled || m_cursorCommitTimer.isActive() || !canCommitCursorOnly())
        return;
    // Hand the latest position to KMS after this batch of input and flip events.
    m_cursorCommitScheduled = true;
    core::mainLoop()->post([output = weakPtr()] {
        if (!output)
            return;
        output->m_cursorCommitScheduled = false;
        output->commitCursorIfIdle();
    });
}

void DrmOutput::commitCursorIfIdle()
{
    if (!canCommitCursorOnly())
        return;
    const auto remaining = m_cursorOnlyAfter - m_now();
    if (remaining > render::OutputScheduler::Duration::zero()) {
        m_cursorCommitTimer.start(remaining);
        return;
    }

    const uint64_t frame = kCursorEventFlag | ++m_cursorCommitCount;
    auto event = std::make_unique<FlipEvent>(FlipEvent {weakPtr(), frame});
    FlipEvent *token = event.get();
    m_events.emplace(frame, std::move(event));

    const PlaneContent cursorContent = m_cursor->atomicPlaneContent();
    if (const int error = m_scanout->commitCursor(cursorContent, token); error != 0) {
        m_events.erase(frame);
        if (error == EBUSY) {
            m_cursorCommitTimer.start(m_scheduler.period());
        } else {
            m_cursor->fallbackToLegacy();
            m_scanout->setHardwareCursor(true);
        }
        return;
    }

    m_cursor->markAtomicCommitted();
    m_cursor->setFlipPending(true);
    if (!m_flipTimeout.isActive())
        m_flipTimeout.start();
}

void DrmOutput::finishCursorFlip()
{
    if (m_events.empty())
        m_flipTimeout.stop();
    else
        m_flipTimeout.start();
    if (m_cursor && m_events.empty())
        m_cursor->setFlipPending(false);
    if (m_events.empty() && m_queuedFrame)
        commitQueued();
    scheduleCursorCommit();
}

} // namespace cutefish::backend
