// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/drm/drm_output.h"

#include <GLES2/gl2.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <xf86drm.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

#include "backend/drm/drm_backend.h"
#include "backend/drm/drm_cursor.h"
#include "backend/drm/drm_device_api.h"
#include "backend/drm/drm_framebuffer.h"
#include "backend/drm/drm_multi_device_planner.h"
#include "backend/drm/drm_selection.h"
#include "base/auto_reset.h"
#include "backend/session/session.h"
#include "core/logging.h"
#include "render/egl_renderer.h"

namespace cutefish::backend {

namespace {

// Mesa's GBM surfaces hold up to four colour buffers.
constexpr int kSwapBuffers = 4;

} // namespace

// A mode without timings has no rate and gets 60 Hz; llround(inf) is undefined.
render::OutputScheduler::Duration DrmOutput::framePeriod(double hz)
{
    return std::chrono::nanoseconds(static_cast<int64_t>(std::llround(1e9 / (hz > 0 ? hz : 60.0))));
}

std::vector<std::string> DrmOutput::requiredConnectorProperties()
{
    return {"CRTC_ID"};
}

std::vector<std::string> DrmOutput::requiredCrtcProperties()
{
    return {"MODE_ID", "ACTIVE"};
}

std::vector<std::string> DrmOutput::requiredPlaneProperties()
{
    return {
            "FB_ID",  "CRTC_ID", "SRC_X",  "SRC_Y",  "SRC_W",
            "SRC_H",  "CRTC_X",  "CRTC_Y", "CRTC_W", "CRTC_H",
    };
}

DrmOutput::DrmOutput(DrmBackend *backend, Config config)
    : DrmOutput(
              backend->device(), std::move(config),
              [backend] {
                  return backend->session() && backend->session()->isActive();
              },
              backend->hasMonotonicPresentationClock())
{
    m_backend = backend;
}

DrmOutput::DrmOutput(DrmDeviceApi *device, Config config, std::function<bool()> sessionActive,
                     bool monotonicClock,
                     std::function<render::OutputScheduler::Time()> now)
    : m_config(std::move(config))
    , m_device(device)
    , m_sessionActive(std::move(sessionActive))
    , m_monotonicClock(monotonicClock)
    , m_now(std::move(now))
    , m_scheduler(framePeriod(refreshRate()), m_now)
{
    m_wakeup.setSingleShot(true);
    m_wakeup.onTimeout = [this] { wakeup(); };
    // Nothing restarts this process, so a lost flip event would stall every
    // later frame.
    m_flipTimeout.setSingleShot(true);
    m_flipTimeout.setInterval(std::chrono::seconds(1));
    m_flipTimeout.onTimeout = [this] { onFlipTimeout(); };
    m_scheduler.setMaxPendingFrames(2);
}

DrmOutput::~DrmOutput()
{
    disable();
    for (auto &entry : m_events)
        retireEvent(std::move(entry.second));
    reportStats();
    m_cursor.reset();
    releaseScanoutBuffers();

    if (m_renderer)
        m_renderer->releaseTarget(m_target.get());
    m_target.reset();
    if (m_gbmSurface)
        gbm_surface_destroy(m_gbmSurface);
    destroyed();
}

std::string DrmOutput::publicName() const
{
    return m_backend && m_backend->usesForeignRenderer()
            ? std::filesystem::path(m_backend->devicePath()).filename().string() + '-'
                    + m_config.name
            : m_config.name;
}

std::unique_ptr<DrmOutput::CompositedBuffer> DrmOutput::acquireComposited(gbm_surface *surface)
{
    m_transferUnavailable = false;
    auto *bo = gbm_surface_lock_front_buffer(surface);
    if (!bo)
        return nullptr;
    DrmFramebuffer *framebuffer = nullptr;
    const base::Size size(gbm_bo_get_width(bo), gbm_bo_get_height(bo));
    auto route = chooseCrossGpuTransfer({!m_backend->usesForeignRenderer(), true, false, false,
                                         int64_t(size.width()) * size.height()});
    if (route == CrossGpuTransfer::Direct) {
        framebuffer = DrmFramebuffer::forBuffer(m_device, bo);
    } else {
        framebuffer = DrmFramebuffer::importForBuffer(m_device, bo);
        if (!framebuffer) {
            route = chooseCrossGpuTransfer({false, false, false,
                                            gbm_bo_get_format(bo) == DRM_FORMAT_XRGB8888,
                                            int64_t(size.width()) * size.height()});
            if (route == CrossGpuTransfer::CpuCopy) {
                glFinish();
                uint32_t stride = 0;
                void *mapData = nullptr;
                auto *pixels =
                        static_cast<uint8_t *>(gbm_bo_map(bo, 0, 0, size.width(), size.height(),
                                                          GBM_BO_TRANSFER_READ, &stride, &mapData));
                if (pixels) {
                    framebuffer = DrmFramebuffer::copyForBuffer(m_device, bo, pixels, stride);
                    gbm_bo_unmap(bo, mapData);
                }
            }
        }
    }
    if (!framebuffer) {
        m_transferUnavailable = m_backend->usesForeignRenderer();
        gbm_surface_release_buffer(surface, bo);
        CF_WARNING(cfDrm) << "No scanout transfer path for" << publicName()
                          << size;
        return nullptr;
    }
    if (m_transferRoute != int(route)) {
        m_transferRoute = int(route);
        CF_INFO(cfDrm) << "Scanout transfer for" << publicName()
                       << (route == CrossGpuTransfer::Direct                 ? "direct"
                                  : route == CrossGpuTransfer::DmaBufImport ? "DMA-BUF import"
                                                                            : "bounded CPU copy");
    }
    auto buffer = std::make_unique<CompositedBuffer>();
    buffer->primary = {m_config.planeId,        framebuffer->id(),     framebuffer->format(),
                       framebuffer->modifier(), base::Rect(size), base::Rect(size)};
    buffer->release = [surface, bo] {
        gbm_surface_release_buffer(surface, bo);
    };
    return buffer;
}

base::Size DrmOutput::size() const
{
    return base::Size(m_config.mode.hdisplay, m_config.mode.vdisplay);
}

compositor::Output::Subpixel DrmOutput::subpixel() const
{
    using S = compositor::Output::Subpixel;
    switch (m_config.subpixel) {
    case DRM_MODE_SUBPIXEL_NONE:
        return S::None;
    case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
        return S::HorizontalRgb;
    case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
        return S::HorizontalBgr;
    case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
        return S::VerticalRgb;
    case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
        return S::VerticalBgr;
    default:
        return S::Unknown;
    }
}

double DrmOutput::refreshRate() const
{
    return cutefish::backend::refreshRate(m_config.mode);
}

double DrmOutput::pacingRate() const
{
    return m_unsynced ? 60.0 : refreshRate();
}

void DrmOutput::setDirectScanoutEnabled(bool enabled)
{
    if (m_directScanoutEnabled == enabled)
        return;
    m_directScanoutEnabled = enabled;
    // Leaving a client buffer on the primary plane after scanout is switched
    // off would freeze the screen on that buffer; ask for a composited frame.
    m_forceFrame = true;
    scheduleFrame(Reason::Scene);
}

bool DrmOutput::supportsColorTransform() const
{
    return m_scanout && m_scanout->supportsColorTransform();
}

void DrmOutput::setColorTransform(const render::ColorTransform &transform)
{
    const bool changed = transform != m_colorTransform;
    const bool blended = blendsColorTransform();
    m_colorTransform = transform;
    if (m_scanout && m_scanout->setColorTransform(transform)) {
        if (m_enabled && m_sessionActive && m_sessionActive()) {
            m_forceFrame = true;
            scheduleFrame(Reason::Scene);
        }
        return;
    }
    if (!changed || supportsColorTransform())
        return;
    // The previous frames' buffers hold the old tint, so frames repaint whole
    // until each is redrawn.
    if (blended && !blendsColorTransform())
        m_untintedRepaints = kSwapBuffers;
    if (m_enabled && m_sessionActive && m_sessionActive()) {
        m_forceFrame = true;
        scheduleFrame(Reason::Scene);
    }
}

void DrmOutput::setSecureScanout(bool secure)
{
    if (m_secureScanout == secure)
        return;
    m_secureScanout = secure;
    // What is on screen was composed under the old policy, so nothing is secure
    // until a new frame is presented.
    withdrawSecurePresentation();
}

void DrmOutput::withdrawSecurePresentation()
{
    m_lastPresentedSecure = false;
    m_scanoutFrame.secure = false;
    m_pendingFrame.secure = false;
    if (m_queuedFrame)
        m_queuedFrame->submitted.secure = false;
    m_forceFrame = true;
    scheduleFrame(Reason::Scene);
}

bool DrmOutput::lastPresentedFrameWasSecure() const
{
    // A DPMS-off or disabled output must not stall a lock waiting for a frame.
    if (!m_enabled || m_blanked)
        return true;
    return m_lastPresentedSecure;
}

bool DrmOutput::isSecureConfiguration(const std::vector<PlaneContent> &planes)
{
    return planes.size() == 1 && !planes.front().isDirectScanout();
}

gbm_surface *DrmOutput::createSurface(const base::Size &size) const
{
    const auto create = [&](bool linear) {
        return gbm_surface_create(m_backend->gbmDevice(), size.width(), size.height(),
                                  m_backend->format(),
                                  GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
                                          | (linear ? GBM_BO_USE_LINEAR : 0));
    };
    // Linear is the one layout every scanout device imports from another GPU.
    const bool foreign = m_backend->usesForeignRenderer();
    gbm_surface *surface = create(m_linearBuffers || foreign);
    if (!surface && foreign && !m_linearBuffers)
        surface = create(false);
    return surface;
}

bool DrmOutput::init(render::EglRenderer *renderer)
{
    m_renderer = renderer;

    m_gbmSurface = createSurface(size());
    if (!m_gbmSurface) {
        CF_CRITICAL(cfDrm) << "Failed to create a GBM surface for" << m_config.name;
        return false;
    }

    m_target = m_renderer->createTarget(m_gbmSurface, size());
    if (!m_target)
        return false;

    auto acquire = [this] {
        return acquireComposited(m_gbmSurface);
    };
    return init(renderer, std::move(m_target), std::move(acquire));
}

bool DrmOutput::init(render::EglRenderer *renderer, std::unique_ptr<render::EglRenderTarget> target,
                     AcquireBuffer acquire, CreateTarget createTarget)
{
    m_renderer = renderer;
    m_target = std::move(target);
    m_acquireBuffer = std::move(acquire);
    m_createTarget = std::move(createTarget);
    if (!m_renderer || !m_target || !m_acquireBuffer)
        return false;
    ScanoutController::Config scanoutConfig;
    scanoutConfig.crtcId = m_config.crtcId;
    scanoutConfig.crtcIndex = m_config.crtcIndex;
    scanoutConfig.connectorId = m_config.connectorId;
    scanoutConfig.crtcProperties = m_config.crtcProperties;
    scanoutConfig.connectorProperties = m_config.connectorProperties;
    scanoutConfig.planes = m_config.planes;
    scanoutConfig.modeSize = size();
    m_scanout = std::make_unique<ScanoutController>(m_device, std::move(scanoutConfig));

    m_scanout->setColorTransform(m_colorTransform);

    m_cursor = DrmCursor::create(m_device, m_config.crtcId);
    m_scanout->setHardwareCursor(m_cursor != nullptr);

    CF_INFO(cfDrm).printf("Render target initialized for %s: %zu overlay planes, hardware cursor %s",
                          m_config.name.c_str(), m_scanout->maxOverlays(),
                          m_cursor ? "available" : "unavailable");
    return true;
}

bool DrmOutput::requiresSoftwareBlanking() const
{
    if (!m_backend)
        return false;
    const auto driver = m_backend->driverName();
    return driver == "virtio_gpu" || driver == "vmwgfx"
            || driver == "qxl";
}

bool DrmOutput::configure(const std::vector<Configuration> &configuration, bool testOnly,
                          DrmCommitStatus *status, std::string *diagnostic,
                          DrmDeviceApi **failedDevice)
{
    std::string detail;
    DrmDeviceApi *device = nullptr;
    auto report = [status, diagnostic, failedDevice, &device, &detail](DrmCommitStatus result) {
        if (status)
            *status = result;
        if (failedDevice)
            *failedDevice = result == DrmCommitStatus::Success ? nullptr : device;
        if (diagnostic)
            *diagnostic = drmCommitMessage(result) + (detail.empty() ? "" : "; " + detail);
        return result == DrmCommitStatus::Success;
    };
    std::set<std::pair<DrmDeviceApi *, uint32_t>> connectors, crtcs, primaryPlanes;
    std::map<DrmBackend *, uint64_t> epochs;
    std::vector<std::unique_ptr<base::AutoReset<bool>>> guards;
    for (const auto &change : configuration) {
        auto *output = change.output;
        if (!output || !output->m_scanout || !output->m_device || output->m_configuring
            || connectors.contains({output->m_device, output->m_config.connectorId})
            || crtcs.contains({output->m_device, output->m_config.crtcId})
            || primaryPlanes.contains({output->m_device, output->m_config.planeId}))
            return report(DrmCommitStatus::InvalidRequest);
        device = output->m_device;
        connectors.insert({device, output->m_config.connectorId});
        crtcs.insert({device, output->m_config.crtcId});
        primaryPlanes.insert({device, output->m_config.planeId});
        if (output->m_backend)
            epochs[output->m_backend] = output->m_backend->topologyEpoch();
        guards.push_back(std::make_unique<base::AutoReset<bool>>(output->m_configuring, true));
    }
    const auto valid = [&] {
        for (const auto &[backend, epoch] : epochs)
            if (backend->topologyEpoch() != epoch)
                return false;
        return std::all_of(configuration.begin(), configuration.end(), [](const auto &change) {
            return !change.output->m_retiring && change.output->m_sessionActive
                    && change.output->m_sessionActive();
        });
    };
    if (!device)
        return report(DrmCommitStatus::InvalidRequest);
    if (!valid())
        return report(DrmCommitStatus::Invalidated);
    report(DrmCommitStatus::PreparationFailed);

    struct Prepared {
        DrmOutput *output = nullptr;
        drmModeModeInfo mode = {};
        bool enabled = false;
        bool blanked = false;
        gbm_surface *surface = nullptr;
        std::unique_ptr<render::EglRenderTarget> target;
        std::unique_ptr<CompositedBuffer> buffer;
        uint32_t blob = 0;

        ~Prepared()
        {
            buffer.reset();
            if (target)
                output->m_renderer->releaseTarget(target.get());
            target.reset();
            if (surface)
                gbm_surface_destroy(surface);
            if (blob)
                output->m_device->destroyPropertyBlob(blob);
        }
    };

    std::vector<std::unique_ptr<Prepared>> prepared;

    struct DeviceBatch {
        DrmDeviceApi *device = nullptr;
        AtomicRequest candidate;
        AtomicRequest previous;
        bool committed = false;
        bool restored = false;
    };

    std::vector<std::unique_ptr<DeviceBatch>> batches;
    const auto batchFor = [&](DrmDeviceApi *device) -> DeviceBatch * {
        for (const auto &batch : batches)
            if (batch->device == device)
                return batch.get();
        auto batch = std::make_unique<DeviceBatch>();
        batch->device = device;
        batches.push_back(std::move(batch));
        return batches.back().get();
    };
    for (const auto &change : configuration) {
        auto *output = change.output;
        device = output->m_device;
        report(DrmCommitStatus::PreparationFailed);
        auto *batch = batchFor(device);
        auto item = std::make_unique<Prepared>();
        item->output = output;
        item->mode = change.mode;
        item->enabled = change.enabled;
        item->blanked = change.enabled && change.blanked;
        std::vector<PlaneContent> planes;
        if (change.enabled) {
            const base::Size size(change.mode.hdisplay, change.mode.vdisplay);
            if (size.isEmpty() || (!output->m_backend && !output->m_createTarget)
                || !device->createPropertyBlob(&change.mode, sizeof(change.mode), &item->blob))
                return false;
            if (output->m_createTarget) {
                item->target = output->m_createTarget(size);
            } else {
                item->surface = output->createSurface(size);
                if (!item->surface)
                    return false;
                item->target = output->m_renderer->createTarget(item->surface, size);
            }
            if (!item->target
                || !output->fillModesetBuffer(item->target.get(), size,
                                              !item->blanked && !testOnly))
                return false;
            if (output->m_createTarget) {
                item->buffer = output->m_acquireBuffer();
                if (!item->buffer)
                    return false;
                item->buffer->primary.planeId = output->m_config.planeId;
                item->buffer->primary.displayRect = base::Rect(size);
            } else {
                item->buffer = output->acquireComposited(item->surface);
                if (!item->buffer)
                    return report(output->m_transferUnavailable
                                          ? DrmCommitStatus::TransferUnavailable
                                          : DrmCommitStatus::PreparationFailed);
            }
            planes.push_back(item->buffer->primary);
        }
        // A blocked session must clear the application planes here, or a DPMS
        // wake shows the previous session's frame.
        const auto request = output->m_scanout->buildRequest(
                planes, true, item->blob, !change.enabled, output->m_secureScanout);
        batch->candidate.properties.insert(batch->candidate.properties.end(),
                                           request.properties.begin(), request.properties.end());
        auto oldPlanes = output->m_pendingFrame.planes.empty() ? output->m_scanoutFrame.planes
                                                               : output->m_pendingFrame.planes;
        if (!output->m_enabled && oldPlanes.empty()) {
            PlaneContent disabled;
            disabled.planeId = output->m_config.planeId;
            oldPlanes.push_back(disabled);
        }
        const auto previous = output->m_scanout->buildRequest(oldPlanes, true, output->m_modeBlobId,
                                                              !output->m_enabled);
        batch->previous.properties.insert(batch->previous.properties.end(),
                                          previous.properties.begin(), previous.properties.end());
        prepared.push_back(std::move(item));
    }
    DrmCommitStatus outcome = DrmCommitStatus::Success;
    if (testOnly) {
        for (const auto &batch : batches) {
            device = batch->device;
            DrmCommitCoordinator coordinator(batch->device);
            outcome = coordinator.execute(batch->candidate, true, DRM_MODE_ATOMIC_ALLOW_MODESET,
                                          nullptr, valid);
            if (outcome != DrmCommitStatus::Success)
                break;
        }
        return report(outcome);
    }
    std::vector<DrmDeviceTransaction> transactions;
    for (const auto &owned : batches) {
        auto *batch = owned.get();
        std::string key = std::to_string(transactions.size());
        for (const auto &change : configuration)
            if (change.output->m_device == batch->device && change.output->m_backend)
                key = change.output->m_backend->devicePath();
        transactions.push_back({key,
                             [&, batch] {
                                 device = batch->device;
                                 DrmCommitCoordinator coordinator(batch->device);
                                 outcome = coordinator.execute(batch->candidate, true,
                                                               DRM_MODE_ATOMIC_ALLOW_MODESET,
                                                               nullptr, valid);
                                 return outcome == DrmCommitStatus::Success;
                             },
                             [&, batch] {
                                 device = batch->device;
                                 if (!valid()) {
                                     outcome = DrmCommitStatus::Invalidated;
                                     return false;
                                 }
                                 batch->committed = batch->device->atomicCommit(
                                         batch->candidate, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
                                 if (!batch->committed)
                                     outcome = DrmCommitStatus::CommitFailed;
                                 return batch->committed;
                             },
                             [batch] {
                                 batch->restored = batch->device->atomicCommit(
                                         batch->previous, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
                                 return batch->restored;
                             }});
    }
    const auto committed = DrmMultiDeviceCommitCoordinator::run(transactions, 0, [&] {
        return valid() ? 0 : 1;
    });
    if (!committed.succeeded) {
        detail = committed.failedDevice + ": " + committed.message;
        CF_WARNING(cfDrm) << detail;
        if (outcome == DrmCommitStatus::Success)
            outcome = DrmCommitStatus::Invalidated;
        for (const auto &batch : batches)
            if (batch->committed && !batch->restored)
                outcome = DrmCommitStatus::RollbackFailed;
        if (outcome != DrmCommitStatus::RollbackFailed)
            return report(outcome);
    }
    std::vector<std::pair<DrmOutput *, uint64_t>> discarded;
    for (auto &item : prepared) {
        auto *output = item->output;
        auto *batch = batchFor(output->m_device);
        if (!committed.succeeded && (!batch->committed || batch->restored))
            continue;
        output->m_wakeup.stop();
        output->m_scheduler.suspend();
        for (const uint64_t frame : output->dropOutstandingFrames())
            discarded.push_back({output, frame});
        output->releaseScanoutBuffers();
        output->m_bufferFrame = 0;
        output->destroyModeBlob();
        output->m_enabled = item->enabled;
        output->m_blanked = item->blanked;
        if (!item->enabled || item->blanked)
            output->hideCursor();
        output->m_scanout->m_activePlanes.clear();
        if (!item->enabled)
            continue;
        output->m_renderer->releaseTarget(output->m_target.get());
        output->m_target.reset();
        if (output->m_gbmSurface)
            gbm_surface_destroy(output->m_gbmSurface);
        output->m_gbmSurface = std::exchange(item->surface, nullptr);
        output->m_target = std::move(item->target);
        output->m_modeBlobId = std::exchange(item->blob, 0);
        output->m_config.mode = item->mode;
        output->m_scanout->m_config.modeSize = output->size();
        output->m_scanout->m_activePlanes = {output->m_config.planeId};
        output->m_scanout->m_colorPending = false;
        output->m_scanoutFrame.planes = {item->buffer->primary};
        output->m_scanoutFrame.composited = std::move(item->buffer);
        output->m_scanoutFrame.secure =
                output->m_secureScanout && isSecureConfiguration(output->m_scanoutFrame.planes);
        output->m_lastPresentedSecure = output->m_scanoutFrame.secure;
        output->m_scheduler.setPeriod(framePeriod(output->pacingRate()));
        if (committed.succeeded && !item->blanked) {
            output->m_scheduler.resume();
            output->m_forceFrame = true;
            output->scheduleFrame(Reason::Scene);
        }
    }
    // Observers must see every route updated before any completion notification.
    for (const auto &[output, frame] : discarded)
        output->frameDiscarded(frame);
    return report(outcome);
}

bool DrmOutput::fillModesetBuffer(render::EglRenderTarget *target, const base::Size &size,
                                  bool copyScanout)
{
    // The CRTC's current frame carries over into the new mode; a blocked
    // session stays black.
    render::DmaBufAttributes current;
    const bool copy = copyScanout && (!m_secureScanout || m_carryOverWhileSecure)
            && m_device->exportCrtcFramebuffer(m_config.crtcId, &current) && current.size == size;
    if (copy)
        m_renderer->setOutputContext({m_config.name, base::Rect(size), size, 1, 0, 0});
    if (!m_renderer->beginFrame(target))
        return false;
    m_renderer->clear({0, 0, 0, 1});
    // Imported once the target is current, and kept until the frame has ended.
    std::shared_ptr<render::Texture> texture = copy ? m_renderer->importDmaBuf(current) : nullptr;
    const base::Rect viewport(size);
    if (texture && m_keepCarriedFrame) {
        // A copy, not the import: the other master may still draw into its buffer.
        m_renderer->renderToTexture(m_carriedFrame, viewport, [&] {
            m_renderer->drawTexture(texture.get(), viewport, 1.0f);
        });
        if (m_carriedFrame)
            m_carriedFrame->label = "carried frame";
    }
    if (texture)
        m_renderer->drawTexture(texture.get(), viewport, 1.0f);
    return m_renderer->endFrame();
}

bool DrmOutput::createModeBlob()
{
    destroyModeBlob();
    if (!m_device->createPropertyBlob(&m_config.mode, sizeof(m_config.mode), &m_modeBlobId)) {
        CF_CRITICAL(cfDrm) << "Failed to create the mode property blob";
        return false;
    }
    return true;
}

void DrmOutput::destroyModeBlob()
{
    if (m_modeBlobId) {
        m_device->destroyPropertyBlob(m_modeBlobId);
        m_modeBlobId = 0;
    }
}

bool DrmOutput::enable()
{
    if (m_enabled)
        return true;
    if (!m_target || !m_renderer || !m_scanout || !m_acquireBuffer || !createModeBlob())
        return false;
    if (!m_pendingFrame.planes.empty() || !m_scanoutFrame.planes.empty()) {
        // Resume without requiring a third GBM buffer while both old frames may
        // still be held. A blocking modeset first establishes known scanout.
        const bool keepPending = !m_pendingFrame.planes.empty();
        SubmittedFrame &retained = keepPending ? m_pendingFrame : m_scanoutFrame;
        m_scheduler.resume();
        const auto frame = m_scheduler.begin();
        if (!frame || !commit(retained.planes, true, frame)) {
            m_scheduler.suspend();
            return false;
        }
        m_presentations.submitted(frame, retained.presentation);
        if (keepPending) {
            retire(m_scanoutFrame, render::SyncFence());
            m_scanoutFrame = std::move(m_pendingFrame);
            m_pendingFrame = SubmittedFrame();
        }
        m_bufferFrame = 0;
        m_enabled = true;
        m_outstanding.push_back(frame);
        m_scheduler.submitted(frame);
        scheduleFrame(Reason::Resume);
        CF_INFO(cfDrm) << "Output resumed with retained scanout:" << m_config.name;
        return true;
    }
    m_enabled = true;
    m_scheduler.resume();
    const auto frame = m_scheduler.begin();
    if (!frame || !renderFrame(frame, true)) {
        if (frame)
            frameDiscarded(frame);
        std::erase(m_outstanding, frame);
        m_enabled = false;
        m_scheduler.suspend();
        return false;
    }
    m_scheduler.submitted(frame);
    frameDrawn(frame, aimedNs());
    CF_INFO(cfDrm) << "Output enabled:" << m_config.name;
    return true;
}

bool DrmOutput::retireForReprobe(bool connectorExists, bool keepRoute)
{
    m_retiring = true;
    m_wakeup.stop();
    m_flipTimeout.stop();
    m_scheduler.suspend();
    hideCursor();
    if (!m_sessionActive || !m_sessionActive())
        return false;
    const auto showsClientBuffer = [this] {
        std::vector<const SubmittedFrame *> frames {&m_scanoutFrame, &m_pendingFrame};
        if (m_queuedFrame)
            frames.push_back(&m_queuedFrame->submitted);
        return std::any_of(frames.begin(), frames.end(), [](const SubmittedFrame *frame) {
            return std::any_of(frame->planes.begin(), frame->planes.end(),
                               [](const PlaneContent &content) {
                                   return content.isDirectScanout();
                               });
        });
    };
    if (m_scanout && !(keepRoute && connectorExists && !showsClientBuffer())) {
        auto request = m_scanout->buildRequest({}, true, 0, true);
        if (!connectorExists) {
            std::erase_if(request.properties, [this](const auto &property) {
                return property.objectId == m_config.connectorId;
            });
        }
        if (!m_device->atomicCommit(request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr))
            return false;
    }
    m_enabled = false;
    for (const uint64_t frame : dropOutstandingFrames())
        frameDiscarded(frame);
    releaseScanoutBuffers();
    m_bufferFrame = 0;
    destroyModeBlob();
    m_presentations.abandon();
    m_prepareCallback = {};
    m_renderCallback = {};
    m_beginFrameCallback = {};
    m_scanoutPlanner = {};
    presentationSubmitted = {};
    return true;
}

void DrmOutput::disable()
{
    m_wakeup.stop();
    m_flipTimeout.stop();
    m_scheduler.suspend();
    for (const uint64_t frame : dropOutstandingFrames())
        frameDiscarded(frame);
    // An inactive session may still have old scanout references; both frames
    // are kept until their flip retires or a modeset replaces them.
    if (m_enabled && m_sessionActive && m_sessionActive() && m_scanout && m_scanout->disable()) {
        releaseScanoutBuffers();
        m_bufferFrame = 0;
    }
    if (m_cursor)
        m_cursor->hide();
    m_enabled = false;
    m_lastPresentedSecure = false;
    destroyModeBlob();
}

std::map<uint32_t, std::vector<uint64_t>> DrmOutput::primaryPlaneFormats() const
{
    for (const DrmPlane &plane : m_config.planes) {
        if (plane.id == m_config.planeId)
            return plane.formats;
    }
    return {};
}

std::optional<base::Point> DrmOutput::probedCursor() const
{
    for (const DrmPlane &plane : m_config.planes) {
        if (plane.type != DRM_PLANE_TYPE_CURSOR || plane.properties.value("FB_ID") == 0
            || plane.properties.value("CRTC_ID") != m_config.crtcId)
            continue;
        // Signed properties: an image may hang off the top or left edge.
        return base::Point(int(int64_t(plane.properties.value("CRTC_X"))),
                      int(int64_t(plane.properties.value("CRTC_Y"))));
    }
    return std::nullopt;
}

bool DrmOutput::setCursorImage(const render::CursorImage &image)
{
    if (m_blanked || !m_enabled)
        return true;
    if (!m_cursor)
        return false;
    return m_cursor->setImage(image.pixels, image.size, image.stride, image.hotspot);
}

void DrmOutput::hideCursor()
{
    if (m_cursor)
        m_cursor->hide();
}

void DrmOutput::moveCursor(const base::PointF &position)
{
    if (m_cursor)
        m_cursor->move(position.toPoint());
}

} // namespace cutefish::backend
