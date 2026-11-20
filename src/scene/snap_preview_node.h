// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "render/renderer.h"
#include "render/rounded_shape.h"
#include "scene/node.h"
#include "base/geometry.h"

namespace cutefish::scene {

class SnapPreviewNode : public Node
{
public:
    static constexpr int kInset = 8;
    static constexpr double kRadius = 12;
    static constexpr int kSizeDurationMs = 150;
    static constexpr int kOpacityDurationMs = 50;

    // `clock` returns steady-clock milliseconds, for times outside an output frame.
    explicit SnapPreviewNode(std::function<double()> clock = {});

    // `windowBounds` are in the parent's coordinates; a change restarts the entrance.
    void show(const base::Rect &windowBounds);

    void setColors(const render::Color &fill, const render::Color &outline,
                   const render::Color &highlight);

    static base::RectF entranceBounds(const base::RectF &target, double elapsedMs);
    static float entranceOpacity(double elapsedMs);

    Content content() const override { return Content::Translucent; }

    bool paintsSubtree() const override { return true; }

    bool requiresFullRepaint() const override { return false; }

    bool acceptsInput(const base::PointF &) const override { return false; }

protected:
    void paint(render::Renderer &renderer, const base::Rect &absoluteGeometry) const override;

private:
    std::function<double()> m_now;
    mutable double m_started = -1;
    render::Color m_fill {0.2f, 0.52f, 1, 0.25f};
    render::Color m_outline {0, 0, 0, 0.08f};
    render::Color m_highlight {1, 1, 1, 0.16f};
};

} // namespace cutefish::scene
