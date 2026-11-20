// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wm/window_visual_controller.h"

#include <algorithm>

#include "compositor/surface.h"
#include "decoration/window_visual_style.h"
#include "ui/geometry_conversions.h"
#include "wm/window.h"
#include "wm/window_manager.h"
#include "wm/window_modality_controller.h"

namespace cutefish::wm {

namespace {

void setShellOutlineColors(scene::WindowVisualState &state, const Window &window,
                           const decoration::WindowVisualStyle &style)
{
    const auto *surface = window.rootSurface();
    if (surface && surface->glass().enabled && surface->glass().rim > 0) {
        state.outlineColor = {0, 0, 0, 0};
        state.highlightColor = {0, 0, 0, 0};
        return;
    }
    const bool dock = window.role() == WindowRole::Dock;
    state.outlineColor = dock ? style.dockOutline : style.shellOutline;
    state.highlightColor = dock ? style.dockHighlight : style.highlight;
}

} // namespace

scene::WindowVisualState
WindowVisualController::evaluate(const Window &window, const decoration::WindowVisualStyle &style)
{
    using Mode = scene::WindowVisualMode;
    scene::WindowVisualState state;
    state.shape.bounds = base::RectF(window.presentedBounds());
    state.surfaceOrigin = window.frameInsets().topLeft() + window.surfaceOrigin();
    if (window.isPopup()
        || (window.role() != WindowRole::Normal && window.role() != WindowRole::Sheet)) {
        if (!window.isPopup() && window.role() == WindowRole::Panel)
            return state;
        const auto *surface = window.rootSurface();
        const base::Point origin = state.surfaceOrigin;
        const ui::Path path = surface ? surface->backgroundBlurShape() : ui::Path();
        if (!path.isEmpty()) {
            state.outlinePath = path.translated(origin.x(), origin.y());
            state.outlineShape = {ui::toBase(state.outlinePath.boundingRect()), 0};
            state.drawOutline = true;
            state.outlineWidthPx = style.outlineWidthPx;
            setShellOutlineColors(state, window, style);
            return state;
        }
        if (surface && !surface->bodyShape().isEmpty()) {
            state.outlinePath = surface->bodyShape().translated(origin.x(), origin.y());
            state.outlineShape = {ui::toBase(state.outlinePath.boundingRect()), 0};
            return state;
        }
        const base::Rect content = surface ? base::Rect(surface->contentSize()) : base::Rect();
        const base::Rect body = surface ? surface->backgroundBlurRect() : base::Rect();
        if (!body.intersected(content).isEmpty() && surface->backgroundBlurRadius() > 0) {
            const double radius = std::min({double(surface->backgroundBlurRadius()),
                                           body.width() / 2.0, body.height() / 2.0});
            state.outlineShape = {base::RectF(body.translated(origin)), radius};
            if (!content.contains(body))
                state.outlineClip = base::RectF(content.translated(origin));
            state.drawOutline = true;
            state.outlineWidthPx = style.outlineWidthPx;
            setShellOutlineColors(state, window, style);
        }
        return state;
    }
    const auto *root = window.rootSurface();
    if (!window.isDecorated() && root && !root->bodyShape().isEmpty()) {
        const base::Point origin = state.surfaceOrigin;
        state.outlinePath = root->bodyShape().translated(origin.x(), origin.y());
        state.outlineShape = {ui::toBase(state.outlinePath.boundingRect()), 0};
        return state;
    }
    if (window.isDecorated())
        state.mode = Mode::ServerFrame;
    else
        state.mode = Mode::ServerExterior;
    const auto exterior = window.clientExteriorInsets();
    state.shape.bounds = state.shape.bounds.adjusted(exterior.left, exterior.top, -exterior.right,
                                                     -exterior.bottom);
    state.clipContent = !exterior.isNull();
    if (isRoundedStateType(window.stateType())) {
        state.shape.radius = style.radiusFor(state.shape.bounds.size().toSize());
        state.clipContent = true;
        state.drawOutline = true;
        state.outlineWidthPx = style.outlineWidthPx;
    }
    state.outlineColor = window.isActive() ? style.activeOutline : style.inactiveOutline;
    state.highlightColor = style.highlight;
    if (modalTransient(&window) || !window.isResponsive())
        state.dimOpacity = style.dimOpacity;
    return state;
}

scene::WindowVisualState WindowVisualController::evaluate(const Window &window)
{
    return evaluate(window, decoration::WindowVisualStyle());
}

WindowVisualController::WindowVisualController(Window *window)
    : m_window(window)
{
    if (window->node())
        m_node = static_cast<scene::WindowVisualNode *>(
                window->node()->addChild(std::make_unique<scene::WindowVisualNode>()));
    window->addObserver(this);
    update();
}

WindowVisualController::~WindowVisualController()
{
    m_window->removeObserver(this);
}

void WindowVisualController::update()
{
    if (m_node)
        m_node->setState(evaluate(*m_window, m_window->manager()->visualStyle()));
}

void WindowVisualController::refresh(Window *window)
{
    if (auto *node = window ? dynamic_cast<scene::WindowVisualNode *>(window->visualNode()) : nullptr)
        node->setState(evaluate(*window, window->manager()->visualStyle()));
}

void WindowVisualController::onWindowVisibilityChanged(Window *window, bool)
{
    refresh(window->transientParent());
}

void WindowVisualController::onWindowModalChanged(Window *window)
{
    refresh(window->transientParent());
}

void WindowVisualController::onWindowTransientParentChanged(Window *window, Window *oldParent)
{
    refresh(oldParent);
    refresh(window->transientParent());
}

} // namespace cutefish::wm
