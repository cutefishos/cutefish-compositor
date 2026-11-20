// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "base/geometry.h"

struct wl_client;
struct wl_display;
struct wl_global;
struct wl_resource;

namespace cutefish::compositor {
class CaptureSource;
}

namespace cutefish::render {
class Renderer;
}

namespace cutefish::scene {
class Node;
}

namespace cutefish::ui {
class Bitmap;
}

namespace cutefish::wayland {

class ImageCaptureManager
{
public:
    using Authorize = std::function<bool(const wl_client *)>;
    using ToplevelSourceResolver =
            std::function<std::shared_ptr<compositor::CaptureSource>(wl_resource *handle)>;

    // `cursorBounds` is the cursor's global logical rectangle, empty while hidden. `composited` says
    // whether the frame the output just composited holds what `scene` would paint.
    enum class Composited {
        Unusable,
        WithoutCursor,
        WithCursor,
    };

    struct Painters {
        std::function<void(bool cursors)> scene;
        std::function<Composited()> composited;
        std::function<void()> cursors;
        std::function<base::Rect()> cursorBounds;
        const scene::Node *root = nullptr;
    };

    // `bindable` lets a client hold the managers before it may capture; empty means `authorize`.
    ImageCaptureManager(wl_display *display, Authorize authorize, std::function<void()> repaint,
                        Authorize bindable = {});
    ~ImageCaptureManager();
    ImageCaptureManager(const ImageCaptureManager &) = delete;
    ImageCaptureManager &operator=(const ImageCaptureManager &) = delete;

    // Releases the globals; must run before the display is destroyed.
    void shutdown();

    // While locked no frame is rendered at all; requests made meanwhile complete after the unlock.
    void setCaptureAllowed(std::function<bool()> allowed);

    // Fails every frame in flight as retryable, so a capture asked for before a lock cannot complete after it.
    void failPending();

    void setToplevelSourceResolver(ToplevelSourceResolver resolver);

    // `done` runs on the main loop with the image in the output's buffer orientation and the
    // wl_output transform that turns it upright, or a null image on failure. Dropping the returned
    // handle abandons the request.
    using CaptureDone = std::function<void(ui::Bitmap image, uint32_t transform)>;
    [[nodiscard]] std::shared_ptr<void> capture(std::shared_ptr<compositor::CaptureSource> source,
                                                bool cursors, CaptureDone done);
    static std::shared_ptr<compositor::CaptureSource> outputSource(const std::string &output);

    // Null keeps every session SHM-only.
    void setRenderer(const render::Renderer *renderer);

    wl_global *sourceGlobal() const;
    wl_global *toplevelSourceGlobal() const;
    wl_global *copyGlobal() const;
    bool hasPending(const std::string &output) const;
    void renderPending(render::Renderer &renderer, const Painters &painters);
    void renderPending(render::Renderer &renderer, const std::function<void(bool)> &paint);

    // Filled buffers are delivered now; waiting for the flip would capture every other refresh.
    void drawn(uint64_t generation, uint64_t aimedNs);
    void presented(uint64_t generation, uint64_t monotonicNs);
    void discarded(uint64_t generation);

    // A hardware cursor leaves the scene untouched, so a session that paints cursors needs a composition.
    void cursorChanged();

    struct State;

private:
    std::shared_ptr<State> m_state;
    wl_global *m_sourceGlobal = nullptr;
    wl_global *m_toplevelSourceGlobal = nullptr;
    wl_global *m_copyGlobal = nullptr;
};

} // namespace cutefish::wayland
