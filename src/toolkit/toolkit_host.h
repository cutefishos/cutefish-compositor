// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_TOOLKIT_TOOLKIT_HOST_H_
#define CUTEFISH_TOOLKIT_TOOLKIT_HOST_H_

#include <cstdint>
#include <memory>

#include "base/region.h"
#include "ui/bitmap.h"
#include "ui/embedded_gpu.h"

namespace cutefish::render {
class Renderer;
class Texture;
} // namespace cutefish::render

namespace cutefish::ui {
class Application;
class EmbeddedWindow;
} // namespace cutefish::ui

namespace cutefish::toolkit {

// One per process, created on first use and kept for the process's life.
ui::Application &application();

void setGpuRenderingAllowed(bool allowed);

// Only what no window holds now.
void purgeGpuResources();
// Reads counters, so it needs no context current.
ui::EmbeddedGpu::CacheUsage gpuCacheUsage();

// With the context its GL objects live on current.
void destroyWindow(std::unique_ptr<ui::EmbeddedWindow> &window);

// So the toolkit's once-per-frame GPU work is done once for all of them.
class PanelBatch
{
public:
    PanelBatch();
    ~PanelBatch();
    PanelBatch(const PanelBatch &) = delete;
    PanelBatch &operator=(const PanelBatch &) = delete;
};

// Skia draws into a texture of the renderer's context, or on the CPU when that
// fails.
class PanelTexture
{
public:
    explicit PanelTexture(ui::EmbeddedWindow *window);
    ~PanelTexture();

    // In nanoseconds on CLOCK_MONOTONIC; 0 for now.
    void setFrameTime(uint64_t frameTimeNs) { m_frameTimeNs = frameTimeNs; }

    // Returns whether the pixels changed.
    bool renderImage();
    const ui::Bitmap &image() const { return m_image; }

    // Null when there is nothing to draw.
    render::Texture *texture(render::Renderer &renderer, bool dirty);
    // The next render that needs another size replaces it rather than freeing
    // it under the node.
    std::shared_ptr<render::Texture> sharedTexture(render::Renderer &renderer, bool dirty);
    // In device pixels.
    const base::Region &lastDamage() const { return m_lastDamage; }
    void releaseRenderResources();
    bool lastFrameOnGpu() const { return m_lastOnGpu; }

private:
    std::shared_ptr<render::Texture> gpuTexture(render::Renderer &renderer, bool dirty);
    std::shared_ptr<render::Texture> cpuTexture(render::Renderer &renderer, bool dirty);

    ui::EmbeddedWindow *m_window;
    uint64_t m_frameTimeNs = 0;
    ui::Bitmap m_image;
    double m_imageScale = 0;
    // In image pixels.
    base::Region m_pendingUpload;
    std::shared_ptr<render::Texture> m_cpuTexture;
    std::shared_ptr<render::Texture> m_gpuTexture;
    base::Region m_lastDamage;
    bool m_gpuCurrent = false;
    // The next CPU frame is copied whole.
    bool m_imageBehind = false;
    bool m_lastOnGpu = false;
    // A renderer whose textures are not GL ones: never asked again.
    const render::Renderer *m_cpuRenderer = nullptr;
};

} // namespace cutefish::toolkit

#endif // CUTEFISH_TOOLKIT_TOOLKIT_HOST_H_
