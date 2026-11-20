// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/sub_surface.cc, display.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include "compositor/sub_surface.h"

#include "compositor/surface.h"

namespace cutefish::compositor {

std::unique_ptr<SubSurface> SubSurface::create(Surface* surface, Surface* parent)
{
    // The parent must not be in the child's tree, and a surface with any role,
    // a cursor too, may not take this one.
    if (!surface || !parent || surface->hasRole())
        return nullptr;
    for (auto* ancestor = parent; ancestor; ancestor = ancestor->parentSurface())
        if (ancestor == surface)
            return nullptr;
    return std::unique_ptr<SubSurface>(new SubSurface(surface, parent));
}

SubSurface::SubSurface(Surface* surface, Surface* parent)
    : m_surface(surface)
    , m_parent(parent)
{
    m_surface->m_subSurface = this;
    m_parent->addSubSurface(m_surface);
}

SubSurface::~SubSurface()
{
    if (m_surface) {
        if (m_parent)
            m_parent->removeSubSurface(m_surface);
        m_surface->m_subSurface = nullptr;
        m_surface->updateMapping();
    }
}

void SubSurface::setPosition(const base::Point& position)
{
    if (!m_parent || !m_surface)
        return;
    m_parent->setSubSurfacePosition(m_surface, position);
}

bool SubSurface::placeAbove(Surface* reference)
{
    if (!m_parent || !m_surface)
        return true;
    return m_parent->placeSubSurface(m_surface, reference, true);
}

bool SubSurface::placeBelow(Surface* reference)
{
    if (!m_parent || !m_surface)
        return true;
    return m_parent->placeSubSurface(m_surface, reference, false);
}

void SubSurface::setCommitBehavior(bool synchronized)
{
    // A synchronized commit still waiting for buffers goes to the cache for its
    // parent's commit, in commit order.
    if (m_surface && m_surface->holdCommitBehavior(synchronized))
        return;
    applyCommitBehavior(synchronized);
}

void SubSurface::applyCommitBehavior(bool synchronized)
{
    m_isSynchronized = synchronized;
    // set_desync applies an existing cache at once if no ancestor still
    // synchronizes us.
    if (m_surface && !isSurfaceSynchronized())
        m_surface->commitDesynchronized();
}

bool SubSurface::isSurfaceSynchronized() const
{
    // Synchronized if this or any ancestor is, including desynchronized links
    // within a synchronized tree.
    if (m_isSynchronized)
        return true;
    return m_parent && m_parent->isSynchronized();
}

void SubSurface::onSurfaceDestroying(Surface* surface)
{
    if (surface == m_parent) {
        m_parent->removeSubSurface(m_surface);
        m_parent = nullptr;
        m_surface->updateMapping();
        return;
    }
    if (m_parent)
        m_parent->removeSubSurface(m_surface);
    m_surface = nullptr;
    // No observer registration remains on the parent after the child dies.
    m_parent = nullptr;
}
} // namespace cutefish::compositor
