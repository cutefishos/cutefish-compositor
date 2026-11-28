// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/ozone/platform/drm/gpu/drm_overlay_plane.cc
//           chromium/src/components/viz/service/display/
//               overlay_strategy_fullscreen.cc
//               overlay_strategy_single_on_top.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include "scene/overlay_candidate.h"

#include <algorithm>
#include <cmath>


#include "render/damage.h"
#include "scene/node.h"
#include "scene/surface_node.h"
#include "scene/window_animation.h"
#include "base/region.h"
#include "ui/geometry_conversions.h"

namespace cutefish::scene {

namespace {

// An animation steps only when drawn, so one anywhere in the scene keeps composition going.
bool hasAnimation(const Node &node)
{
    if (!node.isVisible() && !node.paintsWhenHidden())
        return false;
    if (const auto *animation = dynamic_cast<const WindowAnimation *>(&node);
        animation && animation->paintsSubtree())
        return true;
    return std::any_of(node.children().begin(), node.children().end(), [](const Node *child) {
        return hasAnimation(*child);
    });
}

// A blur reads the buffers under its capture coverage, so no overlay inside it may leave composition.
void collectBlurSamples(const Node &node, const base::Rect &outputRect,
                        std::vector<base::Rect> &samples)
{
    if (!node.isVisible())
        return;
    if (const auto *surface = dynamic_cast<const SurfaceNode *>(&node)) {
        const base::Rect absolute = node.absoluteGeometry();
        const base::Point position = node.absolutePosition();
        const auto add = [&](const base::Rect &rect) {
            const base::Rect visible = rect.intersected(absolute).intersected(outputRect);
            if (visible.isEmpty())
                return;
            // Match BackdropFilter::draw's edge-clamped capture coverage.
            constexpr int halo = render::kBackdropBlurSupport + 2;
            base::Rect coverage = visible.adjusted(-halo, -halo, halo, halo);
            coverage.moveLeft(std::clamp(coverage.left(), outputRect.left(),
                                         std::max(outputRect.left(),
                                                  outputRect.right() - coverage.width())));
            coverage.moveTop(std::clamp(coverage.top(), outputRect.top(),
                                        std::max(outputRect.top(),
                                                 outputRect.bottom() - coverage.height())));
            samples.push_back(coverage.intersected(outputRect));
        };
        if (surface->readsBackgroundBlur()) {
            add(surface->backgroundBlurRect().translated(position).united(
                    ui::toBase(surface->backgroundBlurShape().boundingRect())
                            .translated(position)
                            .toEnclosingRect()));
        }
        if (surface->readsSubsurfaceBlur())
            add(surface->subsurfaceBlurRect().translated(position));
    }
    for (const Node *child : node.children())
        collectBlurSamples(*child, outputRect, samples);
}

struct SceneItem {
    base::Rect bounds;
    bool opaque = false;
    // Non-null when the item is one client buffer a plane could show directly.
    SurfaceNode *surface = nullptr;
    std::shared_ptr<render::PresentationContent> presentation;
};

void flatten(Node &node, const base::Rect &outputRect, std::vector<SceneItem> &items)
{
    if (!node.isVisible() && !node.paintsWhenHidden())
        return;

    const base::Rect bounds = node.absolutePaintBounds().intersected(outputRect);

    if (node.paintsSubtree()) {
        if (!bounds.isEmpty())
            items.push_back(SceneItem {bounds, false, nullptr});
        return;
    }

    const auto appendSelf = [&] {
        const Node::Content content = node.content();
        if (content == Node::Content::None || bounds.isEmpty())
            return;
        SceneItem item;
        item.bounds = bounds;
        item.opaque = content == Node::Content::Opaque;
        if (auto *surface = dynamic_cast<SurfaceNode *>(&node))
            item.presentation = surface->presentationContent();
        if (bounds == node.absoluteGeometry())
            item.surface = dynamic_cast<SurfaceNode *>(&node);
        if (item.surface && !item.surface->scanoutBuffer())
            item.surface = nullptr;
        items.push_back(std::move(item));
    };

    // Must match Node::render: the children below the parent, the parent, then the rest.
    const auto &children = node.children();
    const size_t split = std::min(node.childrenBelowParent(), children.size());

    for (size_t i = 0; i < split; ++i)
        flatten(*children[i], outputRect, items);
    appendSelf();
    for (size_t i = split; i < children.size(); ++i)
        flatten(*children[i], outputRect, items);
}

bool isUnscaled(const SurfaceNode &surface, const base::Size &pixels)
{
    const render::ScanoutBuffer *buffer = surface.scanoutBuffer().get();
    return buffer && !surface.hasContentShape() && surface.alpha() == 1.0f
            && surface.textureMapping().isIdentity() && !buffer->scanoutAttributes().yInvert
            && buffer->scanoutAttributes().size == pixels;
}

bool isUnscaled(const SurfaceNode &surface)
{
    return isUnscaled(surface, surface.geometry().size());
}

OverlayCandidate makeCandidate(SurfaceNode &surface, const base::Rect &bounds, bool opaque, int zOrder)
{
    OverlayCandidate candidate;
    candidate.buffer = surface.scanoutBuffer();
    candidate.acquireFence = surface.acquireFence().duplicate();
    if (surface.acquireFence().isValid() && !candidate.acquireFence.isValid())
        return {};
    candidate.displayRect = bounds;
    candidate.sourceRect = base::Rect(base::Point(0, 0), candidate.buffer->scanoutAttributes().size);
    candidate.opaque = opaque;
    candidate.zOrder = zOrder;
    candidate.node = &surface;
    return candidate;
}

// `outputRect` is in logical coordinates and `outputPixels` is the mode.
bool proposeFullscreen(std::vector<SceneItem> &items, const base::Rect &outputRect,
                       const base::Size &outputPixels, OverlayPlan &plan)
{
    if (items.empty())
        return false;

    SceneItem &top = items.back();
    if (!top.surface || !top.opaque || top.bounds != outputRect)
        return false;
    if (!isUnscaled(*top.surface, outputPixels))
        return false;

    plan.primary = makeCandidate(*top.surface, base::Rect(base::Point(), outputPixels), true, 0);
    return plan.primary.isValid();
}

void proposeSingleOnTop(std::vector<SceneItem> &items, const std::vector<base::Rect> &blurSamples,
                        size_t maxOverlays, OverlayPlan &plan)
{
    if (maxOverlays == 0)
        return;

    std::vector<SceneItem *> promoted;
    for (size_t i = items.size(); i-- > 0 && promoted.size() < maxOverlays;) {
        SceneItem &item = items[i];
        if (!item.surface || !item.opaque || !isUnscaled(*item.surface))
            continue;

        bool occluded = false;
        for (size_t above = i + 1; above < items.size() && !occluded; ++above)
            occluded = items[above].bounds.intersects(item.bounds);
        for (const base::Rect &sample : blurSamples)
            occluded = occluded || sample.intersects(item.bounds);
        if (occluded)
            continue;

        promoted.push_back(&item);
    }

    std::reverse(promoted.begin(), promoted.end());
    int zOrder = 1;
    for (SceneItem *item : promoted) {
        auto candidate = makeCandidate(*item->surface, item->bounds, item->opaque, zOrder++);
        if (candidate.isValid())
            plan.overlays.push_back(std::move(candidate));
    }
}

} // namespace

void removeOccludedPresentation(Node &root, const base::Size &logicalSize,
                                render::PresentationSubmission &submission)
{
    std::vector<SceneItem> items;
    flatten(root, base::Rect(base::Point(), logicalSize), items);
    base::Region covered;
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if ((base::Region(it->bounds) - covered).isEmpty())
            submission.remove(it->presentation);
        if (it->opaque)
            covered += it->bounds;
    }
}

OverlayPlan proposeOverlays(Node &root, const base::Size &outputSize, size_t maxOverlays,
                            double outputScale)
{
    OverlayPlan plan;
    if (outputSize.isEmpty() || outputScale <= 0 || hasAnimation(root))
        return plan;

    const base::Rect outputRect(base::Point(0, 0),
                           base::Size(int(std::ceil(outputSize.width() / outputScale)),
                                 int(std::ceil(outputSize.height() / outputScale))));
    std::vector<SceneItem> items;
    flatten(root, outputRect, items);

    if (proposeFullscreen(items, outputRect, outputSize, plan))
        return plan;

    // Overlays are placed at their logical bounds, which only at scale one
    // are the plane's device pixels.
    if (outputScale != 1)
        return plan;
    std::vector<base::Rect> blurSamples;
    collectBlurSamples(root, outputRect, blurSamples);
    proposeSingleOnTop(items, blurSamples, maxOverlays, plan);
    return plan;
}

} // namespace cutefish::scene
