// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scene/window_animation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

#include "base/region.h"
#include "base/transform.h"
#include "scene/surface_node.h"

namespace cutefish::scene {

namespace {

constexpr double kCrossFadeContentShare = 1.0 / 3;

base::Rect subtreeBounds(const Node *node)
{
    base::Rect bounds = node->absolutePaintBounds();
    for (const Node *child : node->children()) {
        if (child->isVisible())
            bounds = bounds.united(subtreeBounds(child));
    }
    return bounds;
}

bool hasPromotedSurface(const Node &node)
{
    if (node.isPromotedToOverlay())
        return true;
    return std::any_of(node.children().begin(), node.children().end(), [](const Node *child) {
        return hasPromotedSurface(*child);
    });
}

void recordPresentation(render::Renderer &renderer, const Node &node)
{
    if (!node.isVisible())
        return;
    if (const auto *surface = dynamic_cast<const SurfaceNode *>(&node);
        surface && surface->texture() && surface->alpha() > 0
        && !node.absoluteGeometry().isEmpty())
        renderer.recordPresentation(surface->presentationContent(), node.absoluteGeometry());
    for (const Node *child : node.children())
        recordPresentation(renderer, *child);
}

const Node &sceneRoot(const Node &node)
{
    const Node *root = &node;
    while (root->parent())
        root = root->parent();
    return *root;
}

base::Rect outputViewport(const render::Renderer &renderer, const Node &node)
{
    const base::Rect viewport = renderer.outputContext().logicalViewport;
    return viewport.isEmpty() ? sceneRoot(node).geometry() : viewport;
}

} // namespace

base::Rect WindowAnimation::absolutePaintBounds() const
{
    base::Rect bounds = absoluteGeometry();
    if (!paintsSubtree())
        return bounds;
    for (const auto *child : children()) {
        if (child->isVisible())
            bounds = bounds.united(subtreeBounds(child));
    }
    if (!animating())
        return bounds.united(m_coverage);
    return bounds.united(m_coverage)
            .united(m_oldCoverage)
            .united(m_presented.bounds.toEnclosingRect())
            .united(m_from.toEnclosingRect())
            .united(m_to.toEnclosingRect());
}

base::Rect WindowAnimation::textureCoverage() const
{
    base::Rect coverage = absoluteGeometry();
    for (const auto *child : children()) {
        if (child->isVisible())
            coverage = coverage.united(subtreeBounds(child));
    }
    return coverage.adjusted(-64, -64, 64, 64);
}

bool WindowAnimation::textureReusable(const base::Rect &coverage, const base::Rect &viewport,
                                      double scale) const
{
    if (!m_textureReusable || !m_texture || animating() || coverage.size() != m_coverage.size()
        || scale != m_textureScale || viewport != m_textureViewport
        || sceneRoot(*this).revision() != m_textureRootRevision || hasPromotedSurface(*this))
        return false;
    // Only whole device pixel moves keep the texture: it is rasterized at the origin's phase.
    const base::PointF moved = base::PointF(coverage.topLeft() - m_coverage.topLeft()) * scale;
    return base::fuzzyIsNull(moved.x() - std::round(moved.x()))
            && base::fuzzyIsNull(moved.y() - std::round(moved.y()));
}

bool WindowAnimation::redrawsSubtree(const base::Rect &viewport, double scale) const
{
    if (!isVisible() || m_closing || m_type == Type::Minimize)
        return false;
    if (m_style == Style::None && settled() && !children().empty())
        return false;
    const base::Rect coverage = textureCoverage();
    if (!animating() && !viewport.isEmpty() && !coverage.intersects(viewport))
        return false;
    if (textureReusable(coverage, viewport, scale))
        return m_directBackdrop && m_textureDamage.hasDamage(*this, coverage, scale);
    return !drawsDirectly(m_textureDamage.hasDamage(*this, coverage, scale));
}

bool WindowAnimation::settled() const
{
    return !animating() && m_type != Type::Minimize && m_opacity >= 1 && !presentedScaled()
            && !m_presentedAt;
}

bool WindowAnimation::drawsDirectly(bool contentChanged) const
{
    if (m_style == Style::Popup)
        return false;
    const double period = m_framePeriod > 0 ? m_framePeriod : 1000.0 / 60;
    return contentChanged && m_contentChanged && m_directFrames < kDirectRefresh - 1
            && m_now() - m_textureDrawnAt < (kDirectRefresh - 0.5) * period && settled()
            && hasOpaqueSurface();
}

bool WindowAnimation::hasOpaqueSurface() const
{
    const std::function<bool(const Node &)> visit = [&](const Node &node) {
        for (const Node *child : node.children()) {
            const auto *surface = dynamic_cast<const SurfaceNode *>(child);
            if ((surface && surface->isOpaque()) || visit(*child))
                return true;
        }
        return false;
    };
    return visit(*this);
}

bool WindowAnimation::renderSiblingsBelow(render::Renderer &renderer, size_t begin,
                                          size_t end) const
{
    if (m_style != Style::Launchpad || !animating() || (!isVisible() && !paintsWhenHidden())) {
        m_belowTextures.clear();
        return false;
    }
    const base::Rect viewport = outputViewport(renderer, *this);
    if (renderer.capturing || renderer.suppressBackgroundBlur
        || !absolutePaintBounds().intersects(viewport))
        return false;
    const auto &siblings = parent()->children();
    for (size_t i = begin; i < end; ++i) {
        const auto *animation = dynamic_cast<const WindowAnimation *>(siblings[i]);
        if ((animation && animation->style() == Style::Launchpad)
            || hasPromotedSurface(*siblings[i]))
            return false;
    }
    const double scale = renderer.rasterScale();
    BelowTexture &below = m_belowTextures[renderer.outputContext().outputId];
    below.damage.setChildRange(begin, end);
    const bool reusable = below.texture && below.scale == scale && below.viewport == viewport
            && below.rootRevision == sceneRoot(*this).revision();
    if (reusable && m_type == Type::Open && !m_closing) {
        if (!below.damage.hasDamage(*parent(), viewport, scale)) {
            for (size_t i = begin; i < end; ++i)
                recordPresentation(renderer, *siblings[i]);
        }
        renderer.drawTexture(below.texture.get(), viewport);
        return true;
    }
    const base::Region damage = below.damage.update(*parent(), viewport, scale);
    if (reusable && damage.isEmpty()) {
        for (size_t i = begin; i < end; ++i)
            recordPresentation(renderer, *siblings[i]);
    } else {
        // The pass suspends presentation recording; what it draws shows this frame.
        render::PresentationSubmission *submission = renderer.presentationSubmission;
        const auto scope = uint64_t(reinterpret_cast<uintptr_t>(&below));
        const auto kept = [&](uint64_t owner) {
            return reusable && renderer.keepsBackdrop(scope, owner);
        };
        const base::Rect repaint = reusable
                ? render::coverSamples(damage.boundingRect(), below.damage.samples(kept))
                : base::Rect();
        const std::vector<render::KeptBackdrop> fresh = below.damage.freshBackdrops(kept);
        if (!renderer.renderToTexture(below.texture, viewport, repaint, [&] {
                renderer.presentationSubmission = submission;
                const render::BackdropPass backdrops(renderer, scope, fresh);
                for (size_t i = begin; i < end; ++i)
                    siblings[i]->render(renderer);
            })) {
            m_belowTextures.clear();
            return false;
        }
        below.scale = scale;
        below.viewport = viewport;
        below.rootRevision = sceneRoot(*this).revision();
        below.texture->label = "launchpad underlay";
    }
    renderer.drawTexture(below.texture.get(), viewport);
    return true;
}

bool WindowAnimation::textureIsCurrent(render::Renderer &renderer, const base::Rect &coverage,
                                       base::Rect *repaint) const
{
    const bool blurSuppressed =
            renderer.suppressBackgroundBlur || presentedScaled() || m_presentedAt;
    const uint64_t scope = renderer.backdropScope;
    const auto kept = [&](uint64_t owner) {
        return renderer.keptBackdropChange(scope, owner).has_value();
    };
    const base::Region damage = blurSuppressed
            ? m_textureDamage.update(*this, coverage, renderer.rasterScale())
            : m_textureDamage.update(*this, coverage, renderer.rasterScale(), 0,
                                     [&](uint64_t owner, const base::Rect &sampled) {
                                         if (const auto change = renderer.keptBackdropChange(scope, owner))
                                             return *change;
                                         return renderer.repaints(sampled) ? sampled : base::Rect();
                                     });
    const bool valid = !m_directBackdrop
            && textureReusable(coverage, outputViewport(renderer, *this), renderer.rasterScale());
    *repaint = !valid ? base::Rect()
            : blurSuppressed
            ? damage.boundingRect()
            : render::coverSamples(damage.boundingRect(), m_textureDamage.samples(kept));
    return valid && damage.isEmpty()
            && (m_textureBlurSuppressed == blurSuppressed || !hasBackgroundBlurs());
}

void WindowAnimation::swapStashedTexture(bool blurSuppressed) const
{
    const bool wanted = m_stashedTexture && m_stashedTexture->blurSuppressed == blurSuppressed;
    const bool other = m_texture && m_textureBlurSuppressed != blurSuppressed;
    if (!wanted && !(other && hasBackgroundBlurs()))
        return;
    if (!m_stashedTexture)
        m_stashedTexture = std::make_unique<StashedTexture>();
    StashedTexture &stash = *m_stashedTexture;
    std::swap(m_texture, stash.texture);
    std::swap(m_textureBlurSuppressed, stash.blurSuppressed);
    std::swap(m_textureMipmapped, stash.mipmapped);
    std::swap(m_textureDamage, stash.damage);
    std::swap(m_textureReusable, stash.reusable);
    std::swap(m_directBackdrop, stash.directBackdrop);
    std::swap(m_textureScale, stash.scale);
    std::swap(m_textureViewport, stash.viewport);
    std::swap(m_textureRootRevision, stash.rootRevision);
    std::swap(m_coverage, stash.coverage);
    std::swap(m_textureBounds, stash.bounds);
    if (!stash.texture)
        m_stashedTexture.reset();
}

bool WindowAnimation::hasBackgroundBlurs() const
{
    const std::function<bool(const Node &)> visit = [&](const Node &node) {
        for (const Node *child : node.children()) {
            const auto *surface = dynamic_cast<const SurfaceNode *>(child);
            if ((surface && surface->hasBackgroundBlur()) || visit(*child))
                return true;
        }
        return false;
    };
    return visit(*this);
}

bool WindowAnimation::readsBackgroundBlurs() const
{
    const std::function<bool(const Node &)> visit = [&](const Node &node) {
        for (const Node *child : node.children()) {
            const auto *surface = dynamic_cast<const SurfaceNode *>(child);
            if ((surface && (surface->readsBackgroundBlur() || surface->readsSubsurfaceBlur()))
                || visit(*child))
                return true;
        }
        return false;
    };
    return visit(*this);
}

void WindowAnimation::renderPresented(render::Renderer &renderer,
                                      const Presentation &presentation) const
{
    m_presentedAt = presentation;
    render(renderer);
    m_presentedAt.reset();
}

render::RoundedShape WindowAnimation::presentedBody(const Presentation &presentation) const
{
    return presentedBody(presentation, presentation.bounds);
}

render::RoundedShape WindowAnimation::presentedBody(const Presentation &presentation,
                                                    const base::RectF &bounds) const
{
    const base::Rect original = m_textureBounds.isEmpty() ? absoluteGeometry() : m_textureBounds;
    render::RoundedShape body = bodyShape(original, bounds);
    const double own = body.radius * std::min(body.radiusScale.width(), body.radiusScale.height());
    const double uniform = std::clamp(presentation.radius, double(0),
                                     std::min(body.bounds.width(), body.bounds.height()) / 2);
    body.radius = own + (uniform - own) * std::clamp(presentation.uniform, 0.0, 1.0);
    body.radiusScale = base::SizeF(1, 1);
    return body;
}

void WindowAnimation::drawPresented(render::Renderer &renderer, const Sample &value) const
{
    renderer.pushRoundedClip(presentedBody(*m_presentedAt, value.bounds));
    drawPresentedBlurs(renderer, value);
    draw(renderer, m_texture.get(), m_coverage, m_textureBounds, value);
    renderer.popRoundedClip();
    Sample outlined = value;
    outlined.opacity *= float(1 - std::clamp(m_presentedAt->uniform, 0.0, 1.0));
    if (outlined.opacity > 0)
        outline(renderer, m_visual, m_textureBounds, outlined);
}

void WindowAnimation::drawPresentedBlurs(render::Renderer &renderer, const Sample &value) const
{
    if (renderer.suppressBackgroundBlur || m_textureBounds.isEmpty())
        return;
    const double sx = value.bounds.width() / m_textureBounds.width();
    const double sy = value.bounds.height() / m_textureBounds.height();
    const double scale = std::min(sx, sy);
    base::AffineTransform transform;
    transform.translate(value.bounds.x(), value.bounds.y());
    transform.scale(sx, sy);
    transform.translate(-m_textureBounds.x(), -m_textureBounds.y());
    const bool reveal = isPresentedHidden();
    const std::function<void(const Node &)> visit = [&](const Node &node) {
        for (const Node *child : node.children()) {
            const auto *surface = dynamic_cast<const SurfaceNode *>(child);
            if (!child->isVisible() && !(reveal && surface && surface->isHiddenWithWindow()))
                continue;
            if (surface && surface->hasBackgroundBlur() && surface->texture()) {
                const base::Rect geometry = surface->absoluteGeometry();
                render::GlassParams glass = surface->glass();
                glass.blurSigma *= float(scale);
                glass.bevel *= float(scale);
                glass.depth *= float(scale);
                glass.displacement *= float(scale);
                glass.ambientOcclusionRadius *= float(scale);
                glass.plainSigma *= float(scale);
                glass.opacity *= value.opacity;
                const ui::Path &shape = surface->backgroundBlurShape();
                renderer.drawBackgroundBlur(
                        transform.mapRect(base::RectF(surface->backgroundBlurRect().translated(
                                                  geometry.topLeft())))
                                .toEnclosingRect(),
                        base::roundToInt(surface->backgroundBlurRadius() * scale),
                        transform.mapRect(base::RectF(geometry)).toEnclosingRect(), glass,
                        shape.isEmpty() ? ui::Path()
                                        : shape.translated(geometry.x(), geometry.y()).transformed(transform));
            }
            visit(*child);
        }
    };
    visit(*this);
}

void WindowAnimation::render(render::Renderer &renderer) const
{
    if ((!isVisible() && m_type != Type::Minimize && !m_closing && !m_presentedAt) || isRetired())
        return;
    // Sampled per output frame, not per draw.
    m_frameTime = renderer.frameTime;
    m_framePeriod = renderer.framePeriod;
    if (m_firstFrame) {
        auto *self = const_cast<WindowAnimation *>(this);
        const base::Rect geometry = absoluteGeometry();
        self->start(Type::Open, geometry, geometry, false);
        if (m_handoff) {
            const base::RectF &relative = m_handoff->bounds;
            self->m_from = base::RectF(geometry.x() + relative.x() * geometry.width(),
                                  geometry.y() + relative.y() * geometry.height(),
                                  relative.width() * geometry.width(),
                                  relative.height() * geometry.height());
            self->m_startOpacity = m_handoff->opacity;
            self->m_presented = {m_from, m_startOpacity, 0};
            self->m_handoff.reset();
        }
    }
    // A configure acknowledgement alone does not replace the old client buffer.
    if (m_running && isCrossFade(m_type) && !m_boundsContentReady && m_oldTexture) {
        Sample held = m_presented;
        held.opacity *= m_opacity;
        draw(renderer, m_oldTexture.get(), m_oldCoverage, m_oldBounds, held);
        outline(renderer, m_oldVisual, m_oldBounds, held);
        return;
    }
    if (m_running && m_started < 0) {
        m_firstStepAt = now();
        const bool leadIn =
                (m_style == Style::Window && (m_type == Type::Open || m_type == Type::Close))
                || (m_style == Style::Launchpad && m_type == Type::Close);
        m_started = leadIn ? m_firstStepAt - renderer.framePeriod : m_firstStepAt;
        m_firstStep = true;
    } else if (m_running && std::exchange(m_firstStep, false) && renderer.framePeriod > 0
               && now() - m_firstStepAt > renderer.framePeriod) {
        m_started += now() - m_firstStepAt - renderer.framePeriod;
    }

    const bool frozen = (m_closing || m_type == Type::Minimize) && !isPresentedHidden();
    if (frozen && !m_texture) {
        m_snapshot = nullptr;
        m_landingSnapshot = nullptr;
        m_holding = false;
        if (std::exchange(m_running, false) && m_finished)
            m_finished();
        return;
    }
    if (!frozen) {
        for (const Node *child : children()) {
            if (const auto *visual = dynamic_cast<const WindowVisualNode *>(child))
                m_visual = visual->state();
        }
        const base::Rect coverage = textureCoverage();
        const base::Rect viewport = renderer.outputContext().logicalViewport;
        if (!animating() && !m_presentedAt && !viewport.isEmpty() && !coverage.intersects(viewport))
            return;
        // In texture space; the draw below scales them to morphedRadius().
        std::optional<render::Renderer::WindowCorners> corners;
        if (morphsExterior()) {
            const Sample sample = current();
            const base::Rect geometry = absoluteGeometry();
            if (!sample.bounds.isEmpty()) {
                corners = render::Renderer::WindowCorners {
                        morphedRadius(sample),
                        base::SizeF(geometry.width() / sample.bounds.width(),
                               geometry.height() / sample.bounds.height())};
            }
        }
        // The pass suspends presentation recording, but this snapshot is shown in the same frame.
        render::PresentationSubmission *submission = renderer.presentationSubmission;
        const bool blurSuppressed =
                renderer.suppressBackgroundBlur || presentedScaled() || m_presentedAt;
        swapStashedTexture(blurSuppressed);
        if (m_style == Style::None && settled() && !renderer.capturing && !children().empty()
            && !renderer.suppressBackgroundBlur) {
            m_texture.reset();
            m_textureReusable = false;
            m_directBackdrop = false;
            Node::render(renderer);
            m_presented = {base::RectF(absoluteGeometry()), 1, 1};
            return;
        }
        const bool contentChanged =
                m_textureDamage.hasDamage(*this, coverage, renderer.rasterScale());
        const bool direct = !renderer.suppressBackgroundBlur && drawsDirectly(contentChanged);
        if (!renderer.capturing
            && (renderer.frameTime < 0 || renderer.frameTime != m_directFrameTime)) {
            m_directFrameTime = renderer.frameTime;
            m_directFrames = direct ? m_directFrames + 1 : 0;
            if (!direct)
                m_textureDrawnAt = m_now();
            m_contentChanged = contentChanged && settled() && m_texture;
        }
        const bool liveBlur = !renderer.capturing && !renderer.suppressBackgroundBlur
                && !blurSuppressed && settled() && !contentChanged
                && textureReusable(coverage, outputViewport(renderer, *this), renderer.rasterScale())
                && (m_textureBlurSuppressed == blurSuppressed || !hasBackgroundBlurs())
                && readsBackgroundBlurs();
        base::Rect repaint;
        if (liveBlur && m_directBackdrop) {
            Node::render(renderer);
            m_presented = {base::RectF(absoluteGeometry()), 1, 1};
            return;
        } else if (textureIsCurrent(renderer, coverage, &repaint)) {
            recordPresentation(renderer, *this);
        } else if (liveBlur) {
            m_directBackdrop = true;
            Node::render(renderer);
            m_presented = {base::RectF(absoluteGeometry()), 1, 1};
            return;
        } else if (direct) {
            m_textureReusable = false;
            m_directBackdrop = false;
            Node::render(renderer);
            m_presented = {base::RectF(absoluteGeometry()), 1, 1};
            return;
        } else {
            const bool reusable = !animating() && !hasPromotedSurface(*this);
            const uint64_t backdropScope = renderer.backdropScope;
            if (!renderer.renderToTexture(m_texture, coverage, repaint, [&] {
                    renderer.backdropScope = backdropScope;
                    const bool suppressed = renderer.suppressWindowOutlines;
                    const bool wasBlurSuppressed = renderer.suppressBackgroundBlur;
                    const bool revealed = renderer.revealHiddenSurfaces;
                    renderer.suppressWindowOutlines = true;
                    renderer.suppressBackgroundBlur = blurSuppressed;
                    renderer.revealHiddenSurfaces = isPresentedHidden();
                    renderer.presentationSubmission = submission;
                    renderer.windowCorners = corners;
                    renderSubtree(renderer);
                    renderer.windowCorners.reset();
                    renderer.revealHiddenSurfaces = revealed;
                    renderer.suppressBackgroundBlur = wasBlurSuppressed;
                    renderer.suppressWindowOutlines = suppressed;
                })) {
                m_textureReusable = false;
                m_directBackdrop = false;
                Node::render(renderer);
                if (std::exchange(m_running, false) && m_finished)
                    m_finished();
                return;
            }
            m_texture->label = "window snapshot";
            const base::Rect drawn = repaint.isEmpty() ? coverage : repaint;
            const base::Rect output = outputViewport(renderer, *this);
            const auto &samples = m_textureDamage.samples();
            const bool backdropCurrent = blurSuppressed
                    || std::all_of(samples.begin(), samples.end(), [&](const base::Rect &sampled) {
                        return !sampled.intersects(drawn) || !sampled.intersects(output)
                                || renderer.repaints(sampled);
                    });
            m_textureReusable = reusable && backdropCurrent;
            m_directBackdrop = false;
            m_textureBlurSuppressed = blurSuppressed;
            m_textureMipmapped = false;
            m_textureScale = renderer.rasterScale();
            m_textureViewport = output;
            m_textureRootRevision = sceneRoot(*this).revision();
        }
        m_coverage = coverage;
        m_textureBounds = absoluteGeometry();
        if (m_style == Style::Popup && m_running) {
            auto *self = const_cast<WindowAnimation *>(this);
            self->m_from = self->m_to = base::RectF(m_textureBounds);
        }
    }

    if (m_running && m_type == Type::Minimize)
        renderer.enableTrilinearFiltering(m_texture.get());
    if (m_snapshot && m_type == Type::Minimize && !m_presentedAt)
        readSnapshot(renderer);

    Sample value = (m_running || m_holding) && !isPresentedHidden()
            ? current()
            : Sample {absoluteGeometry(), 1, 1};
    if (!m_presentedAt)
        m_presented = value;
    value.opacity *= m_opacity;
    if (m_scaleFrom != m_scaleTo && m_scaleStarted < 0)
        m_scaleStarted = now();
    if (const double scale = presentationScale(); scale != 1) {
        const base::Rect geometry = absoluteGeometry();
        const base::PointF anchor(geometry.x() + std::clamp<double>(m_scaleAnchor.x(), 0, geometry.width()),
                             geometry.y() + std::clamp<double>(m_scaleAnchor.y(), 0, geometry.height()));
        value.bounds = base::RectF(anchor + (value.bounds.topLeft() - anchor) * scale,
                              value.bounds.size() * scale);
    }
    if (m_presentedAt) {
        const base::Rect geometry = absoluteGeometry();
        if (!geometry.isEmpty()) {
            const base::RectF &to = m_presentedAt->bounds;
            const double sx = to.width() / geometry.width();
            const double sy = to.height() / geometry.height();
            const base::PointF from = value.bounds.topLeft() - base::PointF(geometry.topLeft());
            value.bounds = base::RectF(to.topLeft() + base::PointF(from.x() * sx, from.y() * sy),
                                  base::SizeF(value.bounds.width() * sx, value.bounds.height() * sy));
            if (m_texture && !m_textureMipmapped && std::min(sx, sy) < 0.75) {
                renderer.enableTrilinearFiltering(m_texture.get());
                m_textureMipmapped = true;
            }
        }
        value.opacity *= m_presentedAt->opacity;
    }
    if (m_running && m_type == Type::Minimize && value.progress >= 1 && m_landingSnapshot)
        readSnapshot(renderer, true);

    if ((m_running || m_holding) && m_type == Type::Minimize && value.progress >= 1
        && m_landingTexture && !isPresentedHidden()) {
        renderer.drawTextureF(m_landingTexture.get(), value.bounds, value.opacity);
    } else if (!frozen || m_running || m_holding) {
        const double dissolved = std::min(value.progress / kCrossFadeContentShare, 1.0);
        const float blend = m_running && isCrossFade(m_type) ? 1 - std::pow(1 - dissolved, 2) : 1;
        const float exterior = exteriorOpacity(value);
        if (m_presentedAt) {
            drawPresented(renderer, value);
        } else if (morphsExterior()) {
            drawExteriorMorph(renderer, value, blend);
        } else {
            if (exterior < 1) {
                if (exterior > 0)
                    draw(renderer, m_texture.get(), m_coverage, m_textureBounds, value, exterior);
                renderer.pushRoundedClip(bodyShape(m_textureBounds, value.bounds));
                draw(renderer, m_texture.get(), m_coverage, m_textureBounds, value);
                renderer.popRoundedClip();
            } else {
                draw(renderer, m_texture.get(), m_coverage, m_textureBounds, value);
            }
            if (m_oldTexture && blend < 1) {
                if (m_visual.clipContent)
                    renderer.pushRoundedClip(
                            transformedShape(m_visual.shape, m_textureBounds, value));
                draw(renderer, m_oldTexture.get(), m_oldCoverage, m_oldBounds, value, 1 - blend);
                if (m_visual.clipContent)
                    renderer.popRoundedClip();
            }
            Sample outlined = value;
            outlined.opacity *= exterior;
            outline(renderer, m_visual, m_textureBounds, outlined);
        }
    }
    if (m_scaleFrom != m_scaleTo) {
        if (presentationScale() == m_scaleTo)
            m_scaleFrom = m_scaleTo;
        changed();
    }
    if (m_running) {
        if (value.progress >= 1) {
            m_running = false;
            m_holding = m_holdAtEnd && m_type == Type::Minimize;
            m_oldTexture.reset();
            if (m_finished)
                m_finished();
        }
        changed();
    }
}

} // namespace cutefish::scene
