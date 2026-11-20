// Copyright 2015 The Chromium Authors
// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/rotator/screen_rotation_animator.cc
//           chromium/src/ash/rotator/screen_rotation_animation.cc
//           chromium/src/ui/gfx/geometry/cubic_bezier.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_SCENE_SCREEN_TRANSITION_NODE_H_
#define CUTEFISH_SCENE_SCREEN_TRANSITION_NODE_H_

#include <functional>
#include <map>
#include <memory>

#include <string>

#include "scene/node.h"
#include "base/geometry.h"
#include "base/transform.h"

namespace cutefish::scene {

class ScreenTransitionNode : public Node
{
public:
    struct Output {
        std::unique_ptr<render::Texture> outgoing;
        // In base::AffineTransform degrees: 90, -90 or -180.
        int degrees = 0;
    };

    using Outputs = std::map<std::string, Output>;
    using Paint = std::function<void(render::Renderer &)>;

    static constexpr int kRotationDegrees = 20;
    static constexpr int kRotationDurationMs = 250;

    explicit ScreenTransitionNode(std::function<int64_t()> clock = {});

    // Keyed by OutputRenderContext::outputId; the caller requests a frame at the deadline.
    void start(Outputs outputs, Paint paintIncoming, std::function<bool()> ready, int timeoutMs);

    // A held outgoing frame stays for the next rotation to capture.
    void stopAnimating();

    bool isActive() const { return !m_outputs.empty(); }

    bool isAnimating() const { return m_animationStarted >= 0; }

    void render(render::Renderer &renderer) const override;

    Content content() const override { return isActive() ? Content::Translucent : Content::None; }

    bool paintsSubtree() const override { return isActive(); }

    bool acceptsInput(const base::PointF &) const override { return false; }

    void releaseRenderResources() override;

    static double fastOutLinearIn(double t);

    struct RotationSample {
        double oldDegrees;
        float oldOpacity;
        double newDegrees;
    };

    // `progress` is untweened.
    static RotationSample sampleRotation(int degrees, double progress);

    static base::RectF coverRect(const base::SizeF &source, int degrees, const base::RectF &viewport);

    static base::AffineTransform aboutCenter(const base::PointF &center, double degrees);

private:
    struct OutputState {
        Output output;
        std::unique_ptr<render::Texture> incoming;
        bool incomingFailed = false;
    };

    void finish() const;

    mutable std::map<std::string, OutputState> m_outputs;
    mutable Paint m_paintIncoming;
    mutable std::function<bool()> m_ready;
    std::function<int64_t()> m_now;
    int64_t m_deadline = 0;
    mutable int64_t m_animationStarted = -1;
};

} // namespace cutefish::scene

#endif // CUTEFISH_SCENE_SCREEN_TRANSITION_NODE_H_
