// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/desks/desk_animation_base.cc and
//           desk_animation_impl.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#include "wm/desks/desk_animation.h"

#include <set>
#include <utility>

#include "base/event_loop.h"
#include "core/main_loop.h"
#include "wm/desks/desk.h"
#include "wm/window.h"
#include "wm/window_manager.h"

namespace cutefish::wm {

DeskAnimationBase::DeskAnimationBase(DesksController *controller, int endingDesk,
                                     bool isContinuousGestureAnimation)
    : m_controller(controller)
    , m_node(controller->switchNode())
    , m_endingDesk(endingDesk)
    , m_isContinuousGestureAnimation(isContinuousGestureAnimation)
    , m_screenshotTimeout(core::mainLoop())
{
    m_screenshotTimeout.setSingleShot(true);
    m_screenshotTimeout.setInterval(std::chrono::milliseconds(kEndingScreenshotTimeoutMs));
    m_screenshotTimeout.onTimeout = [this] {
        activateTargetDeskWithoutAnimation();
    };
}

DeskAnimationBase::~DeskAnimationBase()
{
    if (!m_finished)
        releaseNode();
}

void DeskAnimationBase::releaseNode()
{
    m_screenshotTimeout.stop();
    if (m_node) {
        m_node->setEndingScreenshotCallback({});
        m_node->setFinishedCallback({});
        m_node->stop();
    }
    m_controller->notify([](DesksObserver *observer) {
        observer->onDeskSwitchAnimationFinished();
    });
}

void DeskAnimationBase::launch()
{
    m_controller->notify([](DesksObserver *observer) {
        observer->onDeskSwitchAnimationLaunching();
    });

    if (!m_node || !startNode()) {
        activateTargetDeskWithoutAnimation();
        return;
    }
    // Both run from inside a frame, which is no place to change the scene.
    m_node->setEndingScreenshotCallback([this] {
        core::mainLoop()->post([animation = m_weakFactory.getWeakPtr()] {
            if (animation)
                animation->onEndingDeskScreenshotTaken();
        });
    });
    m_node->setFinishedCallback([this] {
        core::mainLoop()->post([animation = m_weakFactory.getWeakPtr()] {
            // The fingers or another key press may have taken the strip on meanwhile.
            if (animation && !animation->m_node->isAnimating() && !animation->m_node->isTracking())
                animation->onDeskSwitchAnimationFinished();
        });
    });

    // The starting frames cover the desk content now, so the ending desk can be activated unseen.
    onStartingDeskScreenshotTakenInternal(m_endingDesk);
    takeEndingScreenshot(m_endingDesk);
}

void DeskAnimationBase::takeEndingScreenshot(int desk)
{
    m_node->takeEndingScreenshot(desk);
    m_screenshotTimeout.start();
}

void DeskAnimationBase::onEndingDeskScreenshotTaken()
{
    m_screenshotTimeout.stop();
    if (m_isContinuousGestureAnimation)
        return;
    m_node->startAnimation();
}

void DeskAnimationBase::onDeskSwitchAnimationFinished()
{
    if (std::exchange(m_finished, true))
        return;
    m_screenshotTimeout.stop();
    onDeskSwitchAnimationFinishedInternal();
    releaseNode();
    m_controller->onAnimationFinished(this);
}

void DeskAnimationBase::activateDeskDuringAnimation(Desk *desk, bool updateWindowActivation)
{
    m_controller->activateDeskInternal(desk, updateWindowActivation);
}

void DeskAnimationBase::activateTargetDeskWithoutAnimation()
{
    if (std::exchange(m_finished, true))
        return;
    releaseNode();
    onDeskSwitchAnimationFinishedInternal();
    m_controller->onAnimationFinished(this);
}

DeskActivationAnimation::DeskActivationAnimation(DesksController *controller, int startingDesk,
                                                 int endingDesk, DesksSwitchSource source,
                                                 bool updateWindowActivation)
    : DeskAnimationBase(controller, endingDesk, source == DesksSwitchSource::DeskSwitchTouchpad)
    , m_source(source)
    , m_updateWindowActivation(updateWindowActivation)
    , m_startingDesk(startingDesk)
    , m_type(source == DesksSwitchSource::DeskSwitchShortcut
                             || source == DesksSwitchSource::DeskSwitchTouchpad
                     ? scene::DeskSwitchNode::Type::Continuous
                     : source == DesksSwitchSource::MiniViewButton
                     ? scene::DeskSwitchNode::Type::Glide
                     : scene::DeskSwitchNode::Type::Quick)
{
    if (m_isContinuousGestureAnimation)
        m_chosenDesk = startingDesk;
}

bool DeskActivationAnimation::startNode()
{
    auto frames = m_controller->takeScreenshots([this](render::Renderer &renderer) {
        m_node->paintCoveredContent(renderer);
    });
    if (frames.empty())
        return false;
    m_node->setGeometry(m_controller->windowManager()->outputBounds());
    m_node->start(m_type, int(m_controller->desks().size()), m_startingDesk, m_endingDesk,
                  std::move(frames), m_controller->referenceWidth());
    return m_node->isActive();
}

bool DeskActivationAnimation::replace(bool movingLeft, DesksSwitchSource source)
{
    if (source != m_source)
        return false;
    if (m_isContinuousGestureAnimation) {
        m_chosenDesk = m_endingDesk;
        return true;
    }
    if (!m_node->endingScreenshotTaken())
        return false;

    const int ending = m_endingDesk + (movingLeft ? -1 : 1);
    if (ending < 0 || ending >= int(m_controller->desks().size()))
        return false;
    m_endingDesk = ending;

    if (!m_node->replaceAnimation(ending)) {
        onEndingDeskScreenshotTaken();
        return true;
    }
    prepareDeskForScreenshot(ending);
    takeEndingScreenshot(ending);
    return true;
}

bool DeskActivationAnimation::updateSwipeAnimation(double deskDelta, double timeMs)
{
    if (!m_isContinuousGestureAnimation)
        return false;
    const std::optional<int> ending = m_node->updateSwipeAnimation(deskDelta, timeMs);
    if (!ending)
        return true;
    m_endingDesk = *ending;
    prepareDeskForScreenshot(*ending);
    takeEndingScreenshot(*ending);
    return true;
}

bool DeskActivationAnimation::endSwipeAnimation(double timeMs)
{
    if (!m_isContinuousGestureAnimation)
        return false;
    m_endingDesk = m_node->endSwipeAnimation(timeMs);
    m_chosenDesk = -1;
    if (m_node->endingScreenshotTaken())
        stopScreenshotTimeout();
    if (Desk *desk = m_controller->deskAt(m_endingDesk))
        activateDeskDuringAnimation(desk, m_updateWindowActivation);
    return true;
}

void DeskActivationAnimation::onStartingDeskScreenshotTakenInternal(int endingDesk)
{
    prepareDeskForScreenshot(endingDesk);
}

void DeskActivationAnimation::onDeskSwitchAnimationFinishedInternal()
{
    if (Desk *desk = m_controller->deskAt(m_endingDesk))
        activateDeskDuringAnimation(desk, m_updateWindowActivation);
}

void DeskActivationAnimation::prepareDeskForScreenshot(int index)
{
    if (Desk *desk = m_controller->deskAt(index))
        activateDeskDuringAnimation(desk, m_updateWindowActivation);
}

DeskRemovalAnimation::DeskRemovalAnimation(DesksController *controller, int deskToRemove,
                                           int deskToActivate)
    : DeskAnimationBase(controller, deskToActivate, false)
    , m_deskToRemove(deskToRemove)
{
}

bool DeskRemovalAnimation::startNode()
{
    const Desk *removed = m_controller->deskAt(m_deskToRemove);
    auto still = m_controller->takeWindowScreenshots([this, removed](const Window *window) {
        return m_controller->deskOf(window) == removed;
    });
    if (still.empty())
        return false;
    std::set<std::string> outputs;
    for (const auto &[output, frame] : still)
        outputs.insert(output);
    m_node->setGeometry(m_controller->windowManager()->outputBounds());
    m_node->startRemoval(int(m_controller->desks().size()), m_deskToRemove, m_endingDesk,
                         std::move(still), std::move(outputs), m_controller->referenceWidth());
    return m_node->isActive();
}

void DeskRemovalAnimation::onStartingDeskScreenshotTakenInternal(int endingDesk)
{
    // The next phase copies the desk being activated before the removed desk's
    // windows move to it.
    if (Desk *desk = m_controller->deskAt(endingDesk))
        activateDeskDuringAnimation(desk, /*updateWindowActivation=*/false);
}

void DeskRemovalAnimation::onDeskSwitchAnimationFinishedInternal()
{
    Desk *removed = m_controller->deskAt(m_deskToRemove);
    if (!removed)
        return;
    if (removed == m_controller->activeDesk()) {
        m_controller->removeDeskInternal(removed, /*deskSwitched=*/false);
        return;
    }
    m_controller->removeDeskInternal(removed, /*deskSwitched=*/true);
}

} // namespace cutefish::wm
