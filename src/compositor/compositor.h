// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/display.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_COMPOSITOR_COMPOSITOR_H_
#define CUTEFISH_COMPOSITOR_COMPOSITOR_H_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/geometry.h"

#include "base/signal.h"

#include "compositor/output_scale.h"

namespace cutefish::render {
class DmaBufDevice;
class PresentationSubmission;
class Renderer;
struct DmaBufAttributes;
} // namespace cutefish::render

namespace cutefish::scene {
class Node;
class SurfaceNode;
} // namespace cutefish::scene

namespace cutefish::compositor {

class Buffer;
class ShmPool;
class Surface;

// Produced on the renderer's context at the start of every output frame, before
// the surfaces' textures are updated.
class ContentProducer
{
public:
    virtual bool hasPendingContent(const base::Rect &viewport) const = 0;
    virtual void produceContent(render::Renderer &renderer) = 0;

protected:
    virtual ~ContentProducer() = default;
};

class Compositor
{
public:
    struct OutputView {
        std::string id;
        base::Rect bounds;
        OutputScale scale;
        // `id` is the logical display's session id.
        std::string waylandName;

        friend bool operator==(const OutputView &, const OutputView &) = default;
    };

    // `surfaceContainer` may be null.
    explicit Compositor(scene::Node *surfaceContainer);
    ~Compositor();

    // The wl_surface resource takes ownership of the returned Surface.
    std::unique_ptr<Surface> createSurface();

    // Takes ownership of `fd` in every case, including failure.
    std::shared_ptr<ShmPool> createShmPool(int fd, int32_t size);

    // Null when there is no GPU renderer, in which case no linux-dmabuf global
    // is advertised.
    void setDmaBufDevice(render::DmaBufDevice *device) { m_dmaBufDevice = device; }

    // Takes ownership of the plane fds in `attributes` in every case; null when
    // the import fails.
    std::shared_ptr<Buffer> createDmaBufBuffer(render::DmaBufAttributes attributes);

    const std::vector<Surface *> &surfaces() const { return m_surfaces; }

    OutputScale outputScale() const { return m_outputScale; }

    void setOutputScale(OutputScale scale);
    void setOutputViews(std::vector<OutputView> outputs);

    const std::vector<OutputView> &outputViews() const { return m_outputViews; }

    void refreshOutputScales();
    uint32_t preferredScaleFor(const Surface *surface) const;

    std::vector<std::shared_ptr<Surface *>> takeMovedSurfaces();

    bool awaitsFrame(const base::Rect &viewport) const;

    // Null in tests that only exercise protocol behavior.
    scene::Node *surfaceContainer() const { return m_surfaceContainer; }

    // Above every window; hit testing searches it before the surface container.
    void setInputMethodContainer(scene::Node *container) { m_inputMethodContainer = container; }
    scene::Node *inputMethodContainer() const { return m_inputMethodContainer; }

    // Null for none.
    Surface *surfaceForNode(const scene::Node *node) const;

    // Null for none. The producer outlives the compositor or unsets itself.
    void setContentProducer(ContentProducer *producer) { m_contentProducer = producer; }
    ContentProducer *contentProducer() const { return m_contentProducer; }

    void updateTextures(render::Renderer &renderer);

    // Snapshot callbacks immediately before composition. Roll back on failed
    // submission or suspension; only the matching completion may finish them.
    void frameSubmitted(uint64_t generation = 0);
    void frameSubmittedForOutput(const base::Rect &viewport, uint64_t generation);
    // Content spanning outputs waits for one of them to present it, or for its
    // replacement.
    void abandonUnsubmittedPresentation(const base::Rect &viewport,
                                        const render::PresentationSubmission &submission);
    void frameDiscarded(uint64_t generation);
    void frameDrawn(uint32_t timeMs, uint64_t generation = 0);

    void scheduleRepaint(bool decoration = false);

    scene::SurfaceNode *createSurfaceNode(Surface *surface);
    void surfaceDestroyed(Surface *surface);

    base::Signal<bool> repaintNeeded;
    base::Signal<> outputScaleChanged;
    base::Signal<> outputViewsChanged;
    base::Signal<render::Renderer &> outputRendered;
    // `frameTimeMs` is on the renderer.frameTime clock; scene node animations
    // advance here, before the frame is drawn.
    base::Signal<double> frameStarted;
    // `aimedNs` is on the monotonic clock.
    base::Signal<uint64_t, uint64_t> outputDrawn;
    base::Signal<uint64_t, uint64_t> outputPresented;
    base::Signal<uint64_t> outputDiscarded;

private:
    OutputScale m_outputScale;
    std::vector<OutputView> m_outputViews;
    scene::Node *m_surfaceContainer = nullptr;
    scene::Node *m_inputMethodContainer = nullptr;
    render::DmaBufDevice *m_dmaBufDevice = nullptr;
    ContentProducer *m_contentProducer = nullptr;
    std::vector<Surface *> m_surfaces;
    std::unordered_map<const scene::Node *, Surface *> m_surfaceByNode;
    int m_placementIndex = 0;
};

} // namespace cutefish::compositor

#endif // CUTEFISH_COMPOSITOR_COMPOSITOR_H_
