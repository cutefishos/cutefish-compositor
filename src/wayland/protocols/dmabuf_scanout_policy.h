#ifndef CUTEFISH_WAYLAND_PROTOCOLS_DMABUF_SCANOUT_POLICY_H_
#define CUTEFISH_WAYLAND_PROTOCOLS_DMABUF_SCANOUT_POLICY_H_

#include <set>

#include "base/signal.h"
#include "wm/window_observer.h"

namespace cutefish::wm {
class Window;
class WindowManager;
}

namespace cutefish::wayland {

class DmaBufFeedbackManager;

// Must be destroyed before the WindowManager it observes.
class DmaBufScanoutPolicy : public wm::WindowObserver
{
public:
    DmaBufScanoutPolicy(DmaBufFeedbackManager* manager, wm::WindowManager* windowManager);
    ~DmaBufScanoutPolicy() override;

    void onWindowStateTypeChanged(wm::Window* window, wm::WindowStateType oldType) override;

private:
    void onWindowAdded(wm::Window* window);
    void onWindowRemoved(wm::Window* window);
    void apply(wm::Window* window, bool removed);

    DmaBufFeedbackManager* const m_manager;
    wm::WindowManager* const m_windowManager;
    std::set<wm::Window*> m_windows;
    base::Subscription m_windowAdded;
    base::Subscription m_windowRemoved;
};

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PROTOCOLS_DMABUF_SCANOUT_POLICY_H_
