// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wm/toplevel_event_handler.h"

#include <linux/input-event-codes.h>

#include <cmath>
#include <utility>

#include "compositor/compositor.h"
#include "core/logging.h"
#include "input/seat.h"
#include "scene/window_animation.h"
#include "wm/resize_cursor.h"
#include "wm/snap_drag_controller.h"
#include "wm/window.h"
#include "wm/window_manager.h"
#include "wm/window_resizer.h"
#include "wm/window_snap.h"
#include "wm/window_state.h"
#include "wm/wm_event.h"

namespace cutefish::wm {

namespace {

constexpr int kHeldResizeTimeoutMs = 50;

scene::WindowAnimation *animationFor(Window *window)
{
    return dynamic_cast<scene::WindowAnimation *>(window->node());
}

void crossFade(Window *window, const base::Rect &from, scene::WindowAnimation::Type type)
{
    scene::WindowAnimation *animation = animationFor(window);
    if (animation && from != window->bounds())
        animation->start(type, from, window->bounds());
}

} // namespace

ToplevelEventHandler::ToplevelEventHandler(WindowManager *manager)
    : m_manager(manager)
    , m_snapController(std::make_unique<SnapDragController>(manager))
{
}

ToplevelEventHandler::~ToplevelEventHandler()
{
    m_frameStarted.reset();
}

Window *ToplevelEventHandler::draggedWindow() const
{
    return m_resizer ? m_resizer->window() : nullptr;
}

ResizeEdge ToplevelEventHandler::draggedEdge() const
{
    return m_resizer ? m_resizer->edge() : ResizeEdge::None;
}

bool ToplevelEventHandler::beginMove(Window *window, const base::PointF &pointerPosition,
                                     std::optional<int32_t> touchId)
{
    return beginDrag(window, ResizeEdge::None, pointerPosition, touchId);
}

bool ToplevelEventHandler::beginResize(Window *window, ResizeEdge edge,
                                       const base::PointF &pointerPosition,
                                       std::optional<int32_t> touchId)
{
    if (edge == ResizeEdge::None || !window || !window->isResizable())
        return false;
    return beginDrag(window, edge, pointerPosition, touchId);
}

bool ToplevelEventHandler::beginDrag(Window *window, ResizeEdge edge,
                                     const base::PointF &pointerPosition,
                                     std::optional<int32_t> touchId)
{
    if (!window || window->isPopup() || m_resizer)
        return false;

    WindowState *state = window->state();
    if (state->isMinimized() || state->isFullscreen())
        return false;

    base::Rect restoreBounds;
    if (edge == ResizeEdge::None && !state->isNormal())
        restoreBounds = state->restoreBounds();

    m_initialStateType = state->stateType();
    m_initialRestoreBounds = state->restoreBounds();

    m_resizer = std::make_unique<WindowResizer>(window, pointerPosition, edge,
                                                m_manager->workAreaFor(window), restoreBounds,
                                                m_manager->visibleAreaFor(window));
    m_touchId = touchId;
    CF_DEBUG(cfInputTransitions) << "drag begin target" << window << "edge" << toString(edge)
                                << (touchId ? "touch point" : "pointer") << touchId.value_or(-1);
    window->setDraggedInternal(true);
    window->setResizingInternal(edge != ResizeEdge::None);
    if (edge == ResizeEdge::None)
        m_snapController->begin(window, pointerPosition);
    CF_DEBUG(cfCompositor) << "drag started:" << toString(edge) << "from"
                          << window->bounds();
    if (auto *seat = m_manager->seat()) {
        if (touchId) {
            // The client asked to be moved by this point; the rest of the sequence belongs to the drag.
            seat->takeTouchPoint(*touchId);
        } else {
            seat->setCursorOverride(edge == ResizeEdge::None ? input::CursorShape::Grabbing
                                                             : resizeCursor(edge));
            // The client must drop its pointer state before restore changes its hit regions.
            seat->clearPointerFocus();
        }
    }
    return true;
}

bool ToplevelEventHandler::onPointerMotion(const base::PointF &location)
{
    if (!m_resizer)
        return false;
    if (!m_touchId)
        drag(location);
    return true;
}

bool ToplevelEventHandler::onTouchMotion(int32_t id, const base::PointF &location)
{
    if (!m_resizer || m_touchId != id)
        return false;
    drag(location);
    return true;
}

bool ToplevelEventHandler::onTouchUp(int32_t id)
{
    if (!m_resizer || m_touchId != id)
        return false;
    CF_DEBUG(cfInputTransitions) << "drag terminating touch up target" << draggedWindow();
    completeDrag(DragResult::Success);
    return true;
}

void ToplevelEventHandler::onTouchCancel()
{
    if (isTouchDrag())
        completeDrag(DragResult::Success);
}

void ToplevelEventHandler::drag(const base::PointF &location)
{
    compositor::Compositor *compositor = m_manager->compositor();
    if (!m_resizer->isResize() || !compositor) {
        applyMotion(location);
        return;
    }
    if (m_holdingResize) {
        m_heldResize = location;
        return;
    }
    if (!m_frameStarted) {
        m_frameStarted = compositor->frameStarted.connect([this](double) {
            releaseHeldResize();
        });
    }
    holdResize();
    applyMotion(location);
}

void ToplevelEventHandler::holdResize()
{
    m_holdingResize = true;
    m_manager->runLater(std::chrono::milliseconds(kHeldResizeTimeoutMs), [this] {
        releaseHeldResize();
    });
}

void ToplevelEventHandler::releaseHeldResize()
{
    m_holdingResize = false;
    const auto location = std::exchange(m_heldResize, std::nullopt);
    if (!location || !m_resizer)
        return;
    holdResize();
    applyMotion(*location);
}

void ToplevelEventHandler::applyMotion(const base::PointF &location)
{
    const bool hadMoved = m_resizer->hasMovedOrResized();
    Window *window = m_resizer->window();
    const base::Rect previousBounds = window->bounds();
    if (!m_resizer->isResize()) {
        const base::Rect pointer(int(std::floor(location.x())), int(std::floor(location.y())),
                                 1, 1);
        m_resizer->setAreas(m_manager->workAreaFor(window, pointer),
                            m_manager->visibleAreaFor(window, pointer));
    }
    m_resizer->drag(location);
    // A client-facing callback can end the drag or destroy the window from inside that call.
    if (!m_resizer)
        return;

    if (!hadMoved && m_resizer->hasMovedOrResized() && !m_resizer->restoreBounds().isEmpty()) {
        window->state()->clearRestoreBounds();
        // Before the restored look is announced, which makes the shell surface hold the cross-fade.
        crossFade(window, previousBounds, scene::WindowAnimation::Type::DragBounds);
        window->setRestoringFromDragInternal(true);
        if (!m_resizer)
            return;
    } else if (auto *animation = animationFor(window)) {
        animation->setDragTarget(window->bounds());
    }

    // A maximized client has to keep the size it is given, so a resize makes it normal at once.
    if (!hadMoved && m_resizer->hasMovedOrResized() && m_resizer->isResize()
        && window->state()->isMaximized()) {
        window->state()->clearRestoreBounds();
        window->setRestoringFromDragInternal(true);
        if (!m_resizer)
            return;
    }

    if (!m_resizer->isResize() && m_resizer->hasMovedOrResized())
        m_snapController->update(location);
}

bool ToplevelEventHandler::onPointerButtonReleased()
{
    if (!m_resizer)
        return false;
    if (m_touchId)
        return true;
    CF_DEBUG(cfInputTransitions) << "drag terminating release target" << draggedWindow();
    completeDrag(DragResult::Success);
    return true;
}

bool ToplevelEventHandler::onKeyPressed(uint32_t evdevCode)
{
    if (!m_resizer || evdevCode != KEY_ESC)
        return false;
    completeDrag(DragResult::Revert);
    return true;
}

void ToplevelEventHandler::settleDrag(WindowResizer *resizer, SnapType snapped,
                                      const base::Rect &initialRestoreBounds)
{
    if (!resizer->hasMovedOrResized())
        return;

    Window *window = resizer->window();
    WindowState *state = window->state();

    if (state->stateType() != m_initialStateType)
        return;

    if (snapped != SnapType::None) {
        if (!state->hasRestoreBounds()) {
            state->setRestoreBounds(initialRestoreBounds.isEmpty() ? resizer->initialBounds()
                                                                   : initialRestoreBounds);
        }
        switch (snapped) {
        case SnapType::Primary:
            window->snapPrimary();
            break;
        case SnapType::Secondary:
            window->snapSecondary();
            break;
        case SnapType::Maximize:
            window->maximize();
            break;
        case SnapType::None:
            break;
        }
        if (state->stateType() == snapStateType(snapped)) {
            const base::Rect dragged = window->bounds();
            state->onEvent(WMEvent(WMEventType::WorkAreaChanged));
            crossFade(window, dragged, scene::WindowAnimation::Type::Bounds);
        }
        return;
    }

    if (state->isNormal()) {
        state->clearRestoreBounds();
        return;
    }

    if (state->isSnapped() && resizer->isResize()
        && areBoundsValidSnappedBounds(state->stateType(), window->bounds(),
                                       m_manager->workAreaFor(window))) {
        return;
    }
    state->saveCurrentBoundsForRestore();
    state->onEvent(WMEvent(WMEventType::Normal));
}

void ToplevelEventHandler::completeDrag(DragResult result)
{
    if (result == DragResult::Success)
        releaseHeldResize();
    m_heldResize.reset();
    m_holdingResize = false;
    if (!m_resizer)
        return;

    CF_DEBUG(cfInputTransitions) << "drag finish target" << draggedWindow() << "reason"
                                << (result == DragResult::Success ? "release/explicit-success"
                                            : result == DragResult::Revert
                                            ? "escape/explicit-revert"
                                            : "window-destroyed");

    // Out of the member first: callbacks below may start another drag or destroy the window.
    std::unique_ptr<WindowResizer> resizer = std::move(m_resizer);
    m_touchId.reset();
    const base::Rect initialRestoreBounds = std::exchange(m_initialRestoreBounds, base::Rect());
    CF_DEBUG(cfInputTransitions) << "drag capture cleared";
    const SnapType snapped = m_snapController->end();

    if (result != DragResult::WindowDestroyed) {
        Window *window = resizer->window();
        if (result == DragResult::Revert) {
            resizer->revertDrag();
            if (auto *animation = animationFor(window))
                animation->setDragTarget(window->bounds());
            if (!initialRestoreBounds.isEmpty())
                window->state()->setRestoreBounds(initialRestoreBounds);
        } else {
            resizer->completeDrag();
            settleDrag(resizer.get(), snapped, initialRestoreBounds);
        }
        // After the state change, so the window is never drawn tiled at the drag's bounds.
        window->setRestoringFromDragInternal(false);
        window->setResizingInternal(false);
        window->setDraggedInternal(false);
    }

    // Completion callbacks may already have started another drag.
    if (!m_resizer) {
        if (auto *seat = m_manager->seat())
            seat->setCursorOverride(std::nullopt);
    }
}

void ToplevelEventHandler::onWindowRemoving(Window *window)
{
    if (m_resizer && m_resizer->window() == window)
        completeDrag(DragResult::WindowDestroyed);
}

} // namespace cutefish::wm
