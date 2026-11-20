// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/desks/desks_controller.h and desks_util.cc
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_WM_DESKS_DESKS_CONTROLLER_H_
#define CUTEFISH_WM_DESKS_DESKS_CONTROLLER_H_

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "scene/desk_switch_node.h"
#include "wm/activation.h"
#include "wm/window_observer.h"

namespace cutefish::scene {
class SlideOutNode;
}

namespace cutefish::wm {

class Desk;
class DeskAnimationBase;
class Window;
class WindowManager;

enum class DesksCreationRemovalSource {
    Keyboard,
    Api,
    DesksRestore,
    Button,
};

enum class DesksSwitchSource {
    NewDeskShortcut,
    DeskSwitchShortcut,
    WindowActivated,
    DeskSwitchTouchpad,
    IndexedDeskSwitchShortcut,
    ApiSwitch,
    DeskRestored,
    MiniViewButton,
};

class DesksObserver
{
public:
    virtual ~DesksObserver() = default;
    virtual void onDeskAdded(const Desk *desk) { }
    // After the desk left the list; it is destroyed right after.
    virtual void onDeskRemoved(const Desk *desk) { }
    virtual void onDeskReordered(int from, int to) { }
    virtual void onDeskActivationChanged(const Desk *activated, const Desk *deactivated) { }
    virtual void onDeskNameChanged(const Desk *desk) { }
    virtual void onDeskSwitchAnimationLaunching() { }
    virtual void onDeskSwitchAnimationFinished() { }
    virtual void onChosenDeskChanged() { }
    virtual void onWindowDeskChanged(Window *window) { }
};

class DesksController : public ActivationObserver, public WindowObserver
{
public:
    static constexpr int kMaxDesks = 8;

    using Screenshotter = std::function<scene::DeskSwitchNode::Frames(
            const std::function<void(render::Renderer &)> &paint)>;

    explicit DesksController(WindowManager *manager);
    DesksController(const DesksController &) = delete;
    DesksController &operator=(const DesksController &) = delete;
    ~DesksController() override;

    WindowManager *windowManager() const { return m_manager; }

    const std::vector<std::unique_ptr<Desk>> &desks() const { return m_desks; }
    Desk *activeDesk() const { return m_activeDesk; }
    int activeDeskIndex() const { return deskIndex(m_activeDesk); }
    int deskIndex(const Desk *desk) const;
    Desk *deskAt(int index) const;
    Desk *deskById(uint32_t id) const;

    Desk *targetActiveDesk() const;
    Desk *chosenDesk() const;
    Desk *nextDesk() const;
    Desk *previousDesk() const;

    bool areDesksBeingModified() const;
    bool canCreateDesks() const;
    bool canRemoveDesks() const;

    Desk *newDesk(DesksCreationRemovalSource source, const std::string &name = {});

    void removeDesk(Desk *desk, DesksCreationRemovalSource source);

    // Not while a desk animation runs.
    void reorderDesk(int from, int to);

    void activateDesk(Desk *desk, DesksSwitchSource source);

    // False only while the session is blocked.
    bool activateAdjacentDesk(bool goingLeft, DesksSwitchSource source);

    // Deltas are in desks, times in milliseconds on the steady clock.
    bool startSwipeAnimation(bool moveLeft);
    void updateSwipeAnimation(double deskDelta, double timeMs);
    void endSwipeAnimation(double timeMs);

    bool moveWindowFromActiveDeskTo(Window *window, Desk *target);
    void sendToDeskAtIndex(Window *window, int index);

    // Null for a window on every desk.
    Desk *deskOf(const Window *window) const;
    bool belongsToActiveDesk(const Window *window) const;

    bool isVisibleOnAllDesks(const Window *window) const;
    void setVisibleOnAllDesks(Window *window, bool visibleOnAllDesks);

    void setDeskName(Desk *desk, const std::string &name);

    void performHitTheWallAnimation(bool goingLeft);

    void addObserver(DesksObserver *observer);
    void removeObserver(DesksObserver *observer);

    void onWindowAdded(Window *window);
    void onWindowRemoving(Window *window);
    void updateWindow(Window *window);

    // Without both, desks switch at once.
    void setSwitchNode(scene::DeskSwitchNode *node) { m_switchNode = node; }
    scene::DeskSwitchNode *switchNode() const { return m_switchNode; }
    void setWindowMoveNode(scene::SlideOutNode *node) { m_windowMoveNode = node; }
    void setScreenshotter(Screenshotter screenshotter)
    {
        m_screenshotter = std::move(screenshotter);
    }

    DeskAnimationBase *animation() const { return m_animation.get(); }

    void onWindowActivating(Window *gainingActive, Window *losingActive) override;
    void onWindowActivated(Window *, Window *) override { }

    void onWindowTransientParentChanged(Window *window, Window *oldParent) override;

private:
    friend class Desk;
    friend class DeskAnimationBase;
    friend class DeskActivationAnimation;
    friend class DeskRemovalAnimation;

    scene::DeskSwitchNode::Frames takeScreenshots(
            const std::function<void(render::Renderer &)> &paint) const;
    scene::DeskSwitchNode::Frames
    takeWindowScreenshots(const std::function<bool(const Window *)> &filter) const;
    int referenceWidth() const;

    void onAnimationFinished(DeskAnimationBase *animation);
    void launchActivationAnimation(Desk *desk, DesksSwitchSource source);
    void activateDeskInternal(Desk *desk, bool updateWindowActivation);
    void removeDeskInternal(Desk *desk, bool deskSwitched);
    bool moveWindowFromSourceDeskTo(Window *window, Desk *source, Desk *target);
    void performWindowMoveToDeskAnimation(Window *window, bool goingLeft);

    // Null puts it on every desk.
    void assignWindow(Window *window, Desk *desk);
    void updateDeskVisibility();
    void updateDeskVisibility(Window *window);
    bool isDeskWindow(const Window *window) const;
    Window *transientRoot(Window *window) const;
    const Window *transientRoot(const Window *window) const;

    template <typename F>
    void notify(F &&fn)
    {
        for (DesksObserver *observer : std::vector<DesksObserver *>(m_observers)) {
            if (std::find(m_observers.begin(), m_observers.end(), observer) != m_observers.end())
                fn(observer);
        }
    }

    WindowManager *m_manager = nullptr;
    std::vector<std::unique_ptr<Desk>> m_desks;
    Desk *m_activeDesk = nullptr;
    Desk *m_deskToActivate = nullptr;
    std::map<const Window *, Desk *> m_windowDesks;
    std::set<const Window *> m_visibleOnAllDesks;
    bool m_modifying = false;
    std::unique_ptr<DeskAnimationBase> m_animation;
    std::unique_ptr<DeskAnimationBase> m_finishedAnimation;
    scene::DeskSwitchNode *m_switchNode = nullptr;
    scene::SlideOutNode *m_windowMoveNode = nullptr;
    Screenshotter m_screenshotter;
    std::vector<DesksObserver *> m_observers;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_DESKS_DESKS_CONTROLLER_H_
