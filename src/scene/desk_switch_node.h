// Copyright 2019 The Chromium Authors
// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/desks/root_window_desk_switch_animator.cc
//           chromium/src/ash/wm/desks/desks_animations.cc (PerformHitTheWallAnimation)
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_SCENE_DESK_SWITCH_NODE_H_
#define CUTEFISH_SCENE_DESK_SWITCH_NODE_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include <string>

#include "scene/finger_track.h"
#include "scene/motion.h"
#include "scene/node.h"
#include "base/geometry.h"

namespace cutefish::scene {

// Positions are in desk frames: frame k of the strip is fully on screen at position k.
class DeskSwitchNode : public Node
{
public:
    enum class Type {
        Quick,
        Continuous,
        Glide,
    };

    // Keyed by OutputRenderContext::outputId.
    using Frames = std::map<std::string, std::unique_ptr<render::Texture>>;

    static constexpr int kDesksSpacing = 50;
    static constexpr double kEdgePaddingRatio = 0.1;
    static constexpr int kMinDistanceBeforeScreenshot = 40;
    static constexpr int kQuickAnimationDurationMs = 150;
    static constexpr int kQuickFadeInAnimationDurationMs = 100;
    static constexpr int kGlideDurationMs = 320;
    // Critically damped springs, response in seconds.
    static constexpr double kSwitchResponse = 0.5;
    static constexpr double kSettleResponse = 0.4;
    // In desks per second.
    static constexpr double kFlickVelocity = 0.5;
    static constexpr double kRubberBandCoefficient = 0.55;
    static constexpr double kResampleLatencyMs = motion::FingerTrack::kResampleLatencyMs;
    static constexpr double kResampleMaxPredictionMs =
            motion::FingerTrack::kResampleMaxPredictionMs;
    static constexpr double kQuickAnimationTranslationDistanceRatio = 0.25;
    static constexpr int kHitTheWallDurationMs = 150;

    explicit DeskSwitchNode(std::function<int64_t()> clock = {});

    // Phase (1): `frames` are taken before the ending desk is activated.
    void start(Type type, int desks, int startingDesk, int endingDesk, Frames frames, int width);

    void startRemoval(int desks, int removedDesk, int endingDesk, Frames stillFrames,
                      std::set<std::string> outputs, int width);

    // Phase (2): the ending desk is active, copy it.
    void takeEndingScreenshot(int endingDesk);
    bool endingScreenshotTaken() const { return m_captureDesk < 0; }

    // Phase (3).
    void startAnimation();

    // True when `endingDesk` has no frame yet, so the caller has to activate it and take one.
    bool replaceAnimation(int endingDesk);

    // `timeMs` is steady-clock. Returns a desk to activate and copy as the strip nears an end.
    std::optional<int> updateSwipeAnimation(double deskDelta, double timeMs);

    // Returns the desk the strip settles on.
    int endSwipeAnimation(double timeMs);

    int mostVisibleDesk() const;
    bool reachedEdge() const { return m_reachedEdge; }
    int endingDesk() const { return m_endingDesk; }

    void hitTheWall(bool goingLeft, Frames frames, int width);

    void stop();

    bool isActive() const { return !m_frames.empty() || !m_stillFrames.empty(); }
    bool isTracking() const { return m_motion == Motion::Tracking; }
    bool isAnimating() const
    {
        return (m_motion == Motion::Tween || m_motion == Motion::Spring) && !m_finishNotified;
    }

    // Runs from inside render(): must not change the scene.
    void setEndingScreenshotCallback(std::function<void()> callback)
    {
        m_endingScreenshotTaken = std::move(callback);
    }
    void setFinishedCallback(std::function<void()> callback) { m_finished = std::move(callback); }

    void paintCoveredContent(render::Renderer &renderer) const;

    void render(render::Renderer &renderer) const override;
    Content content() const override { return isActive() ? Content::Opaque : Content::None; }
    bool paintsSubtree() const override { return isActive(); }
    bool acceptsInput(const base::PointF &) const override { return false; }
    bool occludesOutput(const render::Renderer &renderer) const override;
    void releaseRenderResources() override;

    double slotDistance(int width) const;

private:
    enum class Motion {
        Still,
        Tracking,
        Tween,
        Spring,
    };

    struct Animation {
        double from = 0;
        double to = 0;
        int durationMs = 0;
    };

    int slotOf(int desk) const;
    void addDesk(int desk);
    void shiftStrip(double by, double now);
    double edgeInSlots() const;
    double rubberBand(double pulled) const;
    double pulledPosition(double shown) const;
    double fingerPosition(double timeMs) const;
    int flickTarget(double position, double velocity) const;
    void updateCoast();
    void settle(double now, double from, double target, double velocity, double response);
    double position(double now) const;
    bool hasSettled(double now, double distance, double scale) const;
    void takeFrame(render::Renderer &renderer, const base::Rect &viewport) const;
    void renderStrip(render::Renderer &renderer, const base::Rect &viewport, double now) const;
    void renderHitTheWall(render::Renderer &renderer, const base::Rect &viewport, double now) const;

    std::function<int64_t()> m_now;
    Type m_type = Type::Continuous;
    int m_desks = 0;
    int m_startingDesk = -1;
    int m_endingDesk = -1;
    int m_width = 0;
    std::set<int> m_strip;
    mutable std::map<std::string, std::map<int, std::unique_ptr<render::Texture>>> m_frames;
    std::map<std::string, std::unique_ptr<render::Texture>> m_stillFrames;
    mutable int m_captureDesk = -1;
    Motion m_motion = Motion::Still;
    double m_position = 0;
    motion::FingerTrack m_fingers;
    std::optional<motion::Spring> m_coast;
    double m_coastStart = 0;
    Animation m_animation;
    motion::Spring m_spring;
    mutable double m_animationStart = -1;
    mutable bool m_finishNotified = false;
    bool m_reachedEdge = false;
    std::optional<bool> m_hitTheWall;
    std::function<void()> m_endingScreenshotTaken;
    std::function<void()> m_finished;
};

} // namespace cutefish::scene

#endif // CUTEFISH_SCENE_DESK_SWITCH_NODE_H_
