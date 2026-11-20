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

class Cursor final : public input::SeatObserver
{
public:
    Cursor(input::Seat &seat, scene::Node &container, int size = defaultSize());
    ~Cursor() override;

    void onSurfaceFocused(compositor::Surface *, compositor::Surface *) override { }

    void onCursorChanged() override;
    void onCursorPositionChanged() override;

    // Null puts the cursor back in the scene.
    void setSink(render::CursorSink *sink);
    void setScale(double scale);
    void setTheme(const std::string &theme, int size);
    const std::string &theme() const { return m_theme; }
    int size() const { return m_size; }
    std::function<void()> themeChanged;
    void paintForCapture(render::Renderer &renderer) const;

    // In global logical coordinates; empty while the cursor is hidden.
    base::RectF captureBounds() const;

    // Reprograms the sink even when nothing changed: the KMS cursor plane keeps its last image.
    void refresh();

    bool isHardware() const { return m_hardware; }

    // In device pixels of the image the plane gets.
    base::Point deviceHotspot() const;

    // While held, neither the sink nor the scene shows the cursor.
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
