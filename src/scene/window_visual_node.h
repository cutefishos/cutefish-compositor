// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once


#include "render/rounded_shape.h"
#include "scene/node.h"
#include "base/geometry.h"
#include "base/transform.h"
#include "ui/path.h"

namespace cutefish::scene {

enum class WindowVisualMode {
    ServerFrame,
    ServerExterior,
    PreserveClient
};

struct WindowVisualState {
    WindowVisualMode mode = WindowVisualMode::PreserveClient;
    render::RoundedShape shape;
    // Empty unless the outline follows a client-described body instead of shape.
    render::RoundedShape outlineShape;
    ui::Path outlinePath;
    // In the same coordinates as outlineShape.
    base::RectF outlineClip;
    base::Point surfaceOrigin;
    render::Color outlineColor;
    render::Color highlightColor {0, 0, 0, 0};
    double outlineWidthPx = 0;
    // Black over the body while a modal transient blocks the window; 0 is none.
    float dimOpacity = 0;
    bool clipContent = false;
    bool drawOutline = false;

    const render::RoundedShape &outlined() const
    {
        return outlineShape.bounds.isEmpty() ? shape : outlineShape;
    }

    friend bool operator==(const WindowVisualState &, const WindowVisualState &) = default;
};

void paintWindowOutline(render::Renderer &renderer, const render::RoundedShape &shape,
                        const WindowVisualState &state, float opacity = 1);

void paintShapeOutline(render::Renderer &renderer, const WindowVisualState &state,
                       const base::AffineTransform &transform, float opacity = 1);

base::PointF clientBodySnap(const render::Renderer &renderer, const WindowVisualState &state,
                       const base::PointF &position);

// Presentation values only; closing snapshots never retain a Window pointer.
class WindowVisualNode : public Node
{
public:
    WindowVisualNode()
        : Node("window-visual")
    {
    }

    void setState(const WindowVisualState &state)
    {
        // Evaluated on every commit; an unchanged state must not damage the window.
        if (state == m_state)
            return;
        if (state.dimOpacity != m_state.dimOpacity) {
            m_dimFrom = m_dimPresented;
            m_dimStarted = -1;
            m_dimFading = true;
        }
        m_state = state;
        setGeometry(state.shape.bounds.toEnclosingRect());
        // Shape coordinates are relative to the window root, not this node.
        setPosition(base::Point());
        notifyChanged();
    }

    base::Rect absolutePaintBounds() const override
    {
        base::RectF bounds = m_state.shape.bounds;
        if (m_state.drawOutline)
            bounds |= m_state.outlined().bounds.adjusted(-1, -1, 1, 1);
        return bounds.translated(absolutePosition()).toEnclosingRect();
    }

    const WindowVisualState &state() const { return m_state; }

    bool requiresFullRepaint() const override { return false; }

    bool paintsSubtree() const override
    {
        return m_state.clipContent || m_state.drawOutline || m_state.dimOpacity > 0 || m_dimFading;
    }

    void render(render::Renderer &renderer) const override;

private:
    float presentedDim(const render::Renderer &renderer) const;

    WindowVisualState m_state;
    mutable float m_dimFrom = 0;
    mutable float m_dimPresented = 0;
    mutable double m_dimStarted = -1;
    mutable bool m_dimFading = false;
};

} // namespace cutefish::scene
