// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "render/damage.h"
#include "render/renderer.h"
#include "scene/node.h"
#include "scene/surface_node.h"
#include "base/region.h"
#include "base/transform.h"
#include "ui/geometry_conversions.h"

namespace cutefish::scene {

// Coverage history follows cc/trees/damage_tracker at Chromium 7acd40ddefff.
class DamageTracker
{
public:
    // With `rootIsViewport`, only changes to the root's children are damage.
    explicit DamageTracker(bool rootIsViewport = false)
        : m_rootIsViewport(rootIsViewport)
    {
    }

    // Needs a root that is the viewport.
    void setChildRange(size_t begin, size_t end)
    {
        m_childBegin = begin;
        m_childEnd = end;
    }

    // Returns what changed behind a blur's sampled region outside the tracked tree.
    using BackdropChanged = std::function<base::Rect(uint64_t, const base::Rect &)>;
    base::Region update(const Node &root, const base::Rect &viewport, double scale = 1,
                   uint32_t transform = 0, const BackdropChanged &backdropChanged = {})
    {
        Frame frame;
        if (m_cached && !backdropChanged && m_cached->matches(root, viewport, scale, transform))
            frame = std::move(m_cached->frame);
        else
            frame = collect(root, viewport, scale, transform, backdropChanged);
        m_cached.reset();
        m_history = std::move(frame.history);
        m_viewport = viewport;
        m_blurs = std::move(frame.blurs);
        return frame.damage;
    }

    // Leaves the history be; the collection is kept for the next update() unless a node changes.
    bool hasDamage(const Node &root, const base::Rect &viewport, double scale = 1,
                   uint32_t transform = 0) const
    {
        m_cached.emplace(Cached {Node::mutations(), &root, viewport, scale, transform,
                                 render::opaqueBackdrop().has_value(),
                                 collect(root, viewport, scale, transform, {})});
        return !m_cached->frame.damage.isEmpty();
    }

    std::vector<base::Rect> samples(const std::function<bool(uint64_t)> &kept = {}) const
    {
        std::vector<base::Rect> result;
        for (const Blur &blur : m_blurs) {
            if (blur.stale || !kept || !kept(blur.owner))
                result.push_back(blur.sampled);
            else if (!blur.read.isEmpty())
                result.push_back(blur.read);
        }
        return result;
    }

    std::vector<render::KeptBackdrop> freshBackdrops(const std::function<bool(uint64_t)> &kept) const
    {
        std::vector<render::KeptBackdrop> result;
        for (const Blur &blur : m_blurs)
            if (!blur.stale && kept(blur.owner))
                result.push_back({blur.owner, blur.changed});
        return result;
    }

private:
    struct Entry {
        base::Rect bounds;
        uint64_t revision;
        uint64_t contentRevision;
    };

    struct Blur {
        base::Rect sampled;
        uint64_t owner;
        bool stale;
        render::BackdropReach reach;
        base::Rect changed;
        base::Rect read;
        base::Rect outside;
    };

    struct Frame {
        base::Region damage;
        std::map<uint64_t, Entry> history;
        std::vector<Blur> blurs;
    };

    struct Cached {
        uint64_t mutations;
        const Node *root;
        base::Rect viewport;
        double scale;
        uint32_t transform;
        bool opaqueBackdrop;
        Frame frame;

        bool matches(const Node &node, const base::Rect &rect, double s, uint32_t t) const
        {
            return mutations == Node::mutations() && root == &node && viewport == rect
                    && scale == s && transform == t
                    && opaqueBackdrop == render::opaqueBackdrop().has_value();
        }
    };

    // Bounds are kept relative to the viewport's origin.
    Frame collect(const Node &root, const base::Rect &viewport, double scale, uint32_t transform,
                  const BackdropChanged &backdropChanged) const
    {
        Frame frame;
        bool filtered = false;
        const base::Point origin = viewport.topLeft();
        struct Step {
            base::Region damage;
            base::Rect blurred;
            size_t blur = SIZE_MAX;
        };
        std::vector<Step> steps;
        const auto visit = [&](const auto &self, const Node &node, bool ancestorChanged,
                               bool redrawn) -> void {
            if (!node.isVisible() && !node.paintsWhenHidden())
                return;
            const auto &children = node.children();
            if (m_rootIsViewport && &node == &root) {
                for (size_t i = m_childBegin; i < std::min(m_childEnd, children.size()); ++i)
                    self(self, *children[i], false, false);
                return;
            }
            const base::Rect absolute = node.absolutePaintBounds();
            const Entry current {absolute.translated(-origin), node.revision(),
                                 node.contentRevision()};
            const auto previous = m_history.find(node.damageId());
            const bool changed = ancestorChanged || previous == m_history.end()
                    || previous->second.bounds != current.bounds
                    || previous->second.revision != current.revision;
            base::Region damage;
            if (changed) {
                damage += current.bounds;
                if (previous != m_history.end())
                    damage += previous->second.bounds;
            } else {
                damage += expandedContentDamage(node,
                                                node.damageSince(previous->second.contentRevision))
                                  .translated(node.absolutePosition() - origin);
            }
            frame.history.emplace(node.damageId(), current);
            filtered |= node.requiresFullRepaint() && absolute.intersects(viewport);
            const bool redrawsChildren =
                    redrawn || (&node != &root && node.redrawsSubtree(viewport, scale));
            const size_t split = std::min(node.childrenBelowParent(), children.size());
            for (size_t i = 0; i < split; ++i)
                self(self, *children[i], changed, redrawsChildren);
            const auto *surface = dynamic_cast<const SurfaceNode *>(&node);
            if (surface && surface->readsSubsurfaceBlur()) {
                if (transform != 0 || scale <= 0) {
                    filtered |= absolute.intersects(viewport);
                } else {
                    const base::Rect blurred = surface->subsurfaceBlurRect().translated(node.absolutePosition());
                    const int halo = render::kBackdropBlurSupport + int(std::ceil(2 / scale));
                    const base::Rect sampled = blurred.adjusted(-halo, -halo, halo, halo);
                    const uint64_t owner = surface->subsurfaceBlurId();
                    const base::Rect outside = backdropChanged
                            ? backdropChanged(owner, sampled).intersected(sampled)
                            : base::Rect();
                    steps.push_back({{}, blurred.adjusted(-1, -1, 1, 1), frame.blurs.size()});
                    frame.blurs.push_back({sampled, owner, changed || redrawn || outside == sampled,
                                           render::backdropReach(surface->subsurfaceBlurFill(), scale),
                                           {}, {}, outside});
                }
            }
            if (surface && surface->readsBackgroundBlur()) {
                const base::Point position = node.absolutePosition();
                const base::Rect blurred =
                        surface->backgroundBlurRect()
                                .translated(position)
                                .united(ui::toBase(surface->backgroundBlurShape().boundingRect())
                                                .translated(position)
                                                .toEnclosingRect())
                                .intersected(absolute);
                if (!blurred.isEmpty()) {
                    if (transform != 0 || scale <= 0) {
                        filtered |= blurred.intersects(viewport);
                    } else {
                        const int halo = render::kBackdropBlurSupport + int(std::ceil(2 / scale));
                        const base::Rect sampled = blurred.adjusted(-halo, -halo, halo, halo);
                        const base::Rect outside = backdropChanged
                                ? backdropChanged(node.damageId(), sampled).intersected(sampled)
                                : base::Rect();
                        steps.push_back({{}, blurred.adjusted(-1, -1, 1, 1), frame.blurs.size()});
                        frame.blurs.push_back({sampled, node.damageId(),
                                               changed || redrawn || outside == sampled,
                                               render::backdropReach(surface->glass(), scale),
                                               {}, {}, outside});
                    }
                }
            }
            steps.push_back({damage});
            for (size_t i = split; i < children.size(); ++i)
                self(self, *children[i], changed, redrawsChildren);
        };
        visit(visit, root, false, false);
        base::Region &damage = frame.damage;
        for (const auto &[id, previous] : m_history)
            if (!frame.history.contains(id))
                damage += previous.bounds;
        const bool full = m_history.empty() || viewport.size() != m_viewport.size() || filtered;
        for (const Step &step : steps) {
            if (step.blur == SIZE_MAX) {
                damage += step.damage;
                continue;
            }
            Blur &blur = frame.blurs[step.blur];
            const base::Rect sampled = blur.sampled.translated(-origin);
            blur.stale = full || blur.stale;
            if (blur.stale) {
                damage += step.blurred.translated(-origin);
                continue;
            }
            const base::Rect changed = damage.intersected(sampled).boundingRect().united(
                    blur.outside.translated(-origin));
            if (changed.isEmpty())
                continue;
            const auto [shown, read] = blur.reach;
            const base::Rect redone = changed.adjusted(-shown, -shown, shown, shown)
                                              .intersected(step.blurred.translated(-origin));
            if (redone.isEmpty())
                continue;
            blur.changed = changed.translated(origin);
            blur.read = changed.adjusted(-read, -read, read, read).intersected(sampled).translated(origin);
            damage += redone;
        }
        if (full)
            damage = base::Rect(base::Point(), viewport.size());
        damage = damage.translated(origin).intersected(viewport);
        return frame;
    }

    static base::Region expandedContentDamage(const Node &node, const base::Region &damage)
    {
        const auto *surface = dynamic_cast<const SurfaceNode *>(&node);
        if (!surface || !surface->texture() || node.geometry().isEmpty() || damage.isEmpty())
            return damage;
        const base::Size size = node.geometry().size(), pixels = surface->texture()->size();
        const base::AffineTransform toBuffer = base::AffineTransform::fromScale(1.0 / size.width(), 1.0 / size.height())
                * surface->textureMapping()
                * base::AffineTransform::fromScale(pixels.width(), pixels.height());
        bool invertible = false;
        const base::AffineTransform toSurface = toBuffer.inverted(&invertible);
        const base::Rect bounds(base::Point(), size);
        if (!invertible)
            return bounds;
        base::Region result;
        for (const base::Rect &rect : damage.intersected(bounds)) {
            const base::RectF source = toBuffer.mapRect(base::RectF(rect)).adjusted(-1, -1, 1, 1);
            result += toSurface.mapRect(source).toEnclosingRect().intersected(bounds);
        }
        return result;
    }

    bool m_rootIsViewport = false;
    size_t m_childBegin = 0;
    size_t m_childEnd = SIZE_MAX;
    std::map<uint64_t, Entry> m_history;
    base::Rect m_viewport;
    std::vector<Blur> m_blurs;
    mutable std::optional<Cached> m_cached;
};

} // namespace cutefish::scene
