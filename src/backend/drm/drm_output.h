// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_BACKEND_DRM_DRM_OUTPUT_H_
#define CUTEFISH_BACKEND_DRM_DRM_OUTPUT_H_

#include <chrono>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <xf86drmMode.h>

#include "backend/drm/drm_commit_coordinator.h"
#include "base/geometry.h"
#include "base/signal.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "compositor/output.h"
#include "core/main_loop.h"
#include "backend/drm/drm_plane.h"
#include "backend/drm/drm_properties.h"
#include "backend/drm/presentation_tracker.h"
#include "backend/drm/scanout_controller.h"
#include "render/cursor_sink.h"
#include "render/gpu_timer.h"
#include "render/output_render_context.h"
#include "render/output_scheduler.h"
#include "render/texture.h"
#include "scene/overlay_candidate.h"

struct gbm_bo;
struct gbm_surface;

namespace cutefish::render {

class EglRenderer;
class EglRenderTarget;
class Renderer;
} // namespace cutefish::render

namespace cutefish::backend {

class DrmBackend;
class DrmCursor;
class DrmDeviceApi;

class DrmOutput : public render::CursorSink
{
public:
    struct Config {
        uint32_t connectorId = 0;
        uint32_t crtcId = 0;
        // Index into drmModeRes, which is what possible_crtcs refers to.
        int crtcIndex = -1;
        uint32_t planeId = 0;
        drmModeModeInfo mode = {};
        std::vector<drmModeModeInfo> modes;
        std::string name;
        std::string edid;
        std::string portPath;
        base::Size physicalSizeMm;
        int subpixel = DRM_MODE_SUBPIXEL_UNKNOWN;
        DrmPropertyMap connectorProperties;
        DrmPropertyMap crtcProperties;
        DrmPropertyMap planeProperties;
        std::vector<DrmPlane> planes;
    };

    static std::vector<std::string> requiredConnectorProperties();
    static std::vector<std::string> requiredCrtcProperties();
    static std::vector<std::string> requiredPlaneProperties();

    DrmOutput(DrmBackend *backend, Config config);
    DrmOutput(DrmDeviceApi *device, Config config, std::function<bool()> sessionActive,
              bool monotonicClock,
              std::function<render::OutputScheduler::Time()> now =
                      render::OutputScheduler::Clock::now);
    ~DrmOutput() override;

    struct CompositedBuffer {
        CompositedBuffer() = default;
        CompositedBuffer(const CompositedBuffer &) = delete;
        CompositedBuffer &operator=(const CompositedBuffer &) = delete;
        PlaneContent primary;
        std::function<void()> release;

        ~CompositedBuffer()
        {
            if (release)
                release();
        }
    };

    using AcquireBuffer = std::function<std::unique_ptr<CompositedBuffer>()>;
    using CreateTarget = std::function<std::unique_ptr<render::EglRenderTarget>(base::Size)>;
    bool init(render::EglRenderer *renderer, std::unique_ptr<render::EglRenderTarget> target,
              AcquireBuffer acquire, CreateTarget createTarget = {});

    uint32_t connectorId() const { return m_config.connectorId; }

    uint32_t crtcId() const { return m_config.crtcId; }

    // In CRTC pixels: the cursor the previous DRM master left at startup.
    std::optional<base::Point> probedCursor() const;

    const std::string &edid() const { return m_config.edid; }

    const std::string &portPath() const
    {
        return m_config.portPath.empty() ? m_config.name : m_config.portPath;
    }

    // `keepRoute`: the replacement drives the same connector from the same
    // CRTC, so the route stays lit until its first commit.
    bool retireForReprobe(bool connectorExists, bool keepRoute = false);

    const std::string &name() const { return m_config.name; }

    std::string publicName() const;

    DrmBackend *backend() const { return m_backend; }

    base::Size size() const;

    // An empty list means the plane only has a legacy format list.
    std::map<uint32_t, std::vector<uint64_t>> primaryPlaneFormats() const;

    base::Size physicalSizeMm() const { return m_config.physicalSizeMm; }

    compositor::Output::Subpixel subpixel() const;

    double refreshRate() const;

    // The mode's refresh, or 60 Hz once flips show the device has no vblank.
    double pacingRate() const;

    const drmModeModeInfo &mode() const { return m_config.mode; }

    const std::vector<drmModeModeInfo> &modes() const { return m_config.modes; }

    bool init(render::EglRenderer *renderer);

    bool enable();

    // Keeps potentially scanned-out BOs until KMS retires them.
    void disable();

    bool isEnabled() const { return m_enabled; }

    bool isBlanked() const { return m_blanked; }

    bool requiresSoftwareBlanking() const;

    void setLinearBuffers(bool linear) { m_linearBuffers = linear; }
    gbm_surface *createSurface(const base::Size &size) const;

    struct Configuration {
        DrmOutput *output;
        drmModeModeInfo mode;
        bool enabled;
        bool blanked = false;
    };

    static bool configure(const std::vector<Configuration> &configuration, bool testOnly,
                          DrmCommitStatus *status = nullptr, std::string *diagnostic = nullptr,
                          DrmDeviceApi **failedDevice = nullptr);

    void setRenderCallback(std::function<void(render::Renderer &, uint64_t)> callback);

    // Runs with the GL context current, before the scene is painted, whether or
    // not it is composited.
    void setPrepareCallback(std::function<void(render::Renderer &, uint64_t)> callback);

    // `double` is the frame time in milliseconds; returns whether the output
    // has anything to draw.
    void setBeginFrameCallback(std::function<bool(double)> callback);

    // Absent, or returning an empty plan, means everything is composited.
    void setScanoutPlanner(std::function<scene::OverlayPlan(const base::Size &, size_t)> planner);

    // Nothing is presented; the next frame repaints in full.
    std::unique_ptr<render::Texture>
    renderOffscreen(const render::OutputRenderContext &context,
                    const std::function<void(render::Renderer &)> &paint);

    void setDirectScanoutEnabled(bool enabled);

    void setKeepCarriedFrame(bool keep) { m_keepCarriedFrame = keep; }

    std::unique_ptr<render::Texture> takeCarriedFrame() { return std::move(m_carriedFrame); }

    void setCarryOverWhileSecure(bool carry) { m_carryOverWhileSecure = carry; }

    // Set before the transition's first frame: every commit then turns off
    // every plane but the composited primary.
    void setSecureScanout(bool secure);

    // Frames drawn before a session transition began must not complete it.
    void withdrawSecurePresentation();

    // A frame drawn mid-fade is not secure, whatever its planes.
    void setSecureContentCheck(std::function<bool(double)> check)
    {
        m_secureContentCheck = std::move(check);
    }

    // The session's presentation barrier waits for this; a dark output counts
    // as secure.
    bool lastPresentedFrameWasSecure() const;

    static bool isSecureConfiguration(const std::vector<PlaneContent> &planes);

    bool supportsColorTransform() const;

    bool blendsColorTransform() const
    {
        return !m_colorTransform.isIdentity() && !supportsColorTransform();
    }

    // Applied through KMS from the next frame on; a CRTC with neither CTM nor
    // gamma LUT gets it blended instead.
    void setColorTransform(const render::ColorTransform &transform);

    bool setCursorImage(const render::CursorImage &image) override;
    void hideCursor() override;
    void moveCursor(const base::PointF &position) override;

    uint64_t directScanoutFrames() const { return m_directScanoutFrames; }

    uint64_t overlayFrames() const { return m_overlayFrames; }

    using Reason = render::OutputScheduler::Reason;
    void scheduleFrame(Reason reason = Reason::Scene);
    void warmUp();
    // Answers whether the frame requested next redraws the whole output, for
    // the scheduler to begin it early enough.
    void setFullRepaintQuery(std::function<bool()> query) { m_fullRepaintQuery = std::move(query); }

    base::WeakPtr<DrmOutput> weakPtr() { return m_weakFactory.getWeakPtr(); }

    struct FlipEvent {
        base::WeakPtr<DrmOutput> output;
        uint64_t frame;
        bool delivered = false;
    };

    void setEventRetirementHandler(std::function<void(std::unique_ptr<FlipEvent>)> handler)
    {
        m_retireEvent = std::move(handler);
    }

    void setFlipTimeout(std::chrono::milliseconds timeout) { m_flipTimeout.setInterval(timeout); }

    static void dispatchPageFlip(FlipEvent event, uint32_t seconds, uint32_t microseconds,
                                 uint32_t sequence);
    void onPageFlip(uint64_t frame, uint32_t seconds, uint32_t microseconds, uint32_t sequence);
    std::shared_ptr<render::PresentationOutput> presentationOutput;
    std::function<void(const render::PresentationSubmission &)> presentationSubmitted;
    void noteCompositorChange() { m_compositorChange = true; }
    std::function<void()> compositorFrameDrawn;
    void reportStats() const;
    std::string statsLine() const;

    // The second value is the refresh it was drawn for, in monotonic
    // nanoseconds.
    base::Signal<uint64_t, uint64_t> frameDrawn;
    // Time in milliseconds, then generation.
    base::Signal<uint32_t, uint64_t> framePresented;
    // Monotonic nanoseconds.
    base::Signal<uint64_t, uint64_t> imagePresented;
    base::Signal<uint64_t> frameDiscarded;
    // The last thing the destructor does; the output still answers.
    base::Signal<> destroyed;

private:
    struct SubmittedFrame {
        render::PresentationSubmission presentation;
        std::vector<PlaneContent> planes;
        // Null when the frame scanned a client buffer out directly.
        std::unique_ptr<CompositedBuffer> composited;
        // Signals when this commit reaches the screen, so it is the previous
        // frame's release fence.
        render::SyncFence releaseFence;
        render::GpuTimer::Mark drawnMark;
        render::SyncFence drawnFence;
        bool secure = false;
    };

    // KMS holds one flip per CRTC, so this is committed the moment the pending
    // flip lands.
    struct QueuedFrame {
        uint64_t frame = 0;
        SubmittedFrame submitted;
    };

    std::unique_ptr<CompositedBuffer> acquireComposited(gbm_surface *surface);
    void commitQueued();
    // Returns their tokens for frameDiscarded once the caller's state is
    // consistent.
    std::vector<uint64_t> dropOutstandingFrames();
    bool renderFrame(uint64_t frame, bool modeset);
    bool renderAttempt(uint64_t frame, bool modeset, bool allowPromotion, bool &prepared,
                       bool retry = false);
    void wakeup();
    void armWakeup();
    // The refresh the frame begun last is drawn for, in monotonic nanoseconds.
    uint64_t aimedNs() const;
    void finishFlip(uint64_t frame, uint32_t timeMs,
                    std::optional<render::OutputScheduler::Time> presented);
    void onFlipTimeout();
    // The kernel keeps the event's address as its cookie, so it must outlive
    // this output.
    void retireEvent(std::unique_ptr<FlipEvent> event);
    bool commit(const std::vector<PlaneContent> &planes, bool modeset, uint64_t frame = 0);
    // Copies the CRTC's current framebuffer when the sizes match and clears it
    // black otherwise.
    bool fillModesetBuffer(render::EglRenderTarget *target, const base::Size &size, bool copyScanout);
    bool createModeBlob();
    void destroyModeBlob();
    void releaseScanoutBuffers();
    void clearPromotions();
    // Hands the buffers of a retired frame back, with the fence that says when
    // the display actually stopped reading them.
    void retire(SubmittedFrame &frame, const render::SyncFence &fence);

    DrmBackend *m_backend = nullptr;
    Config m_config;
    DrmDeviceApi *m_device = nullptr;
    std::function<bool()> m_sessionActive;
    bool m_monotonicClock = false;
    AcquireBuffer m_acquireBuffer;
    CreateTarget m_createTarget;

    render::EglRenderer *m_renderer = nullptr;
    gbm_surface *m_gbmSurface = nullptr;
    std::unique_ptr<render::EglRenderTarget> m_target;

    uint32_t m_modeBlobId = 0;

    // Scanning out now; released only once KMS has moved on.
    gbm_bo *m_scanoutBuffer = nullptr;
    gbm_bo *m_pendingBuffer = nullptr;

    std::function<void(render::Renderer &, uint64_t)> m_renderCallback;
    std::function<void(render::Renderer &, uint64_t)> m_prepareCallback;
    std::function<bool(double)> m_beginFrameCallback;
    std::function<bool()> m_fullRepaintQuery;
    bool m_forceFrame = true;
    bool m_compositorChange = false;
    std::function<scene::OverlayPlan(const base::Size &, size_t)> m_scanoutPlanner;

    std::unique_ptr<ScanoutController> m_scanout;
    std::unique_ptr<DrmCursor> m_cursor;
    // Cleared once the frame is committed, so a later fallback composites them
    // again.
    std::vector<scene::SurfaceNode *> m_promoted;
    PresentationTracker m_presentations;
    SubmittedFrame m_pendingFrame;
    SubmittedFrame m_scanoutFrame;
    std::optional<QueuedFrame> m_queuedFrame;
    render::SyncFence m_lastReleaseFence;
    bool m_directScanoutEnabled = true;
    render::ColorTransform m_colorTransform;
    // Full repaints still owed after a blended tint ends, one per buffer the
    // GBM surface may still hold tinted.
    int m_untintedRepaints = 0;
    bool m_secureScanout = false;
    std::function<bool(double)> m_secureContentCheck;
    bool m_keepCarriedFrame = false;
    bool m_carryOverWhileSecure = false;
    std::unique_ptr<render::Texture> m_carriedFrame;
    bool m_lastPresentedSecure = false;
    uint64_t m_directScanoutFrames = 0, m_overlayFrames = 0;

    bool m_enabled = false;
    bool m_blanked = false;
    bool m_linearBuffers = false;
    bool m_transferUnavailable = false;
    int m_transferRoute = -1;
    bool m_retiring = false;
    bool m_configuring = false;
    std::function<render::OutputScheduler::Time()> m_now;
    render::OutputScheduler m_scheduler;
    base::Timer m_wakeup {core::mainLoop()};
    render::OutputScheduler::Time m_armedDeadline {};
    base::Timer m_flipTimeout {core::mainLoop()};
    std::map<uint64_t, std::unique_ptr<FlipEvent>> m_events;
    std::function<void(std::unique_ptr<FlipEvent>)> m_retireEvent;
    uint64_t m_bufferFrame = 0;
    // Frames whose wl_surface.frame callbacks are still owed, oldest first:
    // committed, queued, or a modeset's; gone once presented or discarded.
    std::vector<uint64_t> m_outstanding;
    uint64_t m_frameCount = 0, m_commits = 0, m_flipCommits = 0, m_completions = 0;
    std::clock_t m_cpuStart = std::clock();
    uint32_t m_lastSequence = 0, m_lastTimeMs = 0;
    bool m_unsynced = false;
    std::chrono::steady_clock::time_point m_statsStart = std::chrono::steady_clock::now();
    // Last, so flip events lose this output before anything else of it goes.
    base::WeakPtrFactory<DrmOutput> m_weakFactory {this};
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_DRM_OUTPUT_H_
