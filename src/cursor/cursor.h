// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <string>

#include "base/geometry.h"
#include "cursor/theme.h"
#include "input/seat_observer.h"
#include "render/cursor_sink.h"
#include "scene/node.h"

namespace cutefish::input {
class Seat;
}

namespace cutefish::cursor {

// Composited in the existing scene, or handed to a hardware cursor plane when
// one is available. No Aura window, helper process or private Wayland connection
// is needed either way.
class Cursor final : public input::SeatObserver
{
public:
    Cursor(input::Seat &seat, scene::Node &container, int size = defaultSize());
    ~Cursor() override;

    void onSurfaceFocused(compositor::Surface *, compositor::Surface *) override { }

    void onCursorChanged() override;
    void onCursorPositionChanged() override;

    // The hardware cursor plane, when the output has one. Null puts the cursor
    // back in the scene. A sink that refuses an image does the same, per image.
    void setSink(render::CursorSink *sink);
    void setScale(double scale);
    void setTheme(const std::string &theme, int size);
    // The theme and logical size in use, after the fallbacks setTheme() applies.
    const std::string &theme() const { return m_theme; }
    int size() const { return m_size; }
    std::function<void()> themeChanged;
    void paintForCapture(render::Renderer &renderer) const;

    // Where paintForCapture() would draw, in global logical coordinates; empty
    // while the cursor is hidden. Capture damage tracking needs it because a
    // hardware cursor moves without touching the scene.
    base::RectF captureBounds() const;

    // Re-derives the image from the seat and reprograms the sink even when
    // nothing the cursor tracks changed. Locking uses it: the KMS cursor plane
    // keeps the last image it was handed, so a session cursor would otherwise
    // still be on screen above the secure scene.
    void refresh();

    // Whether the current image is on the sink rather than in the scene.
    bool isHardware() const { return m_hardware; }

    // The current image's hotspot in the image the plane gets, in device pixels.
    base::Point deviceHotspot() const;

    // While held, neither the sink nor the scene shows the cursor: a compositor
    // starting up leaves the previous one's on the plane until its displays settle.
    void setHeld(bool held);

private:
    // Pushes the current frame to the sink; returns false to composite instead.
    bool pushToSink();
    void updatePresentation();

    render::CursorSink *m_sink = nullptr;
    bool m_hardware = false;
    bool m_held = false;
    double m_scale = 1;
    std::string m_theme = "default";
    int m_size = defaultSize();
    class ImageNode;
    input::Seat &m_seat;
    scene::Node &m_container;
    ImageNode *m_node;
};

} // namespace cutefish::cursor
