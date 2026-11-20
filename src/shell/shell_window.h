// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SHELL_WINDOW_H_
#define CUTEFISH_SHELL_SHELL_WINDOW_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/region.h"
#include "base/signal.h"
#include "input/cursor_shape.h"
#include "render/glass.h"
#include "ui/bitmap.h"
#include "ui/path.h"
#include "ui/window.h"
#include "wm/types.h"
#include "wm/window_manager.h"
#include "wm/window_observer.h"

namespace cutefish::compositor {
class Buffer;
class Surface;
}

namespace cutefish::render {
class Renderer;
}

namespace cutefish::toolkit {
class PanelTexture;
}

namespace cutefish::ui {
class EmbeddedWindow;
class View;
} // namespace cutefish::ui

namespace cutefish::wm {
class ShellSurface;
class Window;
} // namespace cutefish::wm

namespace cutefish::shell {

class PanelBuffer;
class Shell;

// Its frames are the contents of a compositor-side surface with no client,
// given a wm::ShellSurface; input reaches it through the seat.
class ShellWindow : public wm::WindowObserver
{
public:
    ShellWindow(Shell *shell, wm::WindowRole role);
    // The toolkit owns `popup`.
    ShellWindow(ShellWindow *parent, ui::EmbeddedWindow *popup);
    ShellWindow(const ShellWindow &) = delete;
    ShellWindow &operator=(const ShellWindow &) = delete;
    ~ShellWindow() override;

    Shell *shell() const { return m_shell; }
    // Null once a popup's toolkit window is gone.
    ui::EmbeddedWindow *window() const { return m_window; }
    void setContentView(std::unique_ptr<ui::View> view);

    compositor::Surface *surface() const { return m_surface.get(); }
    wm::ShellSurface *shellSurface() const { return m_shellSurface.get(); }
    // Null while hidden.
    wm::Window *wmWindow() const;
    wm::WindowRole role() const { return m_metadata.role; }
    bool isPopup() const { return m_parent != nullptr; }

    void setFocusPolicy(wm::FocusPolicy policy);
    void setPanelBehavior(wm::PanelBehavior behavior);
    // By wl_output name; empty follows the output under the window's position.
    void setOutput(const std::string &name);
    const std::string &outputName() const { return m_metadata.outputName; }
    // By wm::Window::id.
    void setParentWindow(uint64_t windowId);
    void setProtectedFromControl(bool protectedFromControl);
    // Set before the geometry it goes with.
    void setEdgeReach(int reach);
    void setDialog(bool dialog);
    void setExclusiveZone(wm::ScreenEdge edge, int thickness, bool yieldsToMaximized = false);
    void clearExclusiveZone();
    void activate();

    // Geometry in global logical coordinates.

    void setPosition(const base::Point &position);
    void setSize(const base::Size &size);
    base::Size size() const { return m_size; }
    void setGeometry(const base::Rect &geometry);
    base::Rect geometry() const { return base::Rect(m_position, m_size); }
    base::Rect drawnBounds() const;
    double scale() const { return m_scale; }
    bool lastFrameOnGpu() const;

    // In window coordinates; nullopt for the whole window, an empty region for
    // none.
    void setInputRegion(std::optional<base::Region> region);

    // `image.width() / scale` logical pixels with `hotspot` in logical pixels;
    // unset follows the views.
    struct Cursor {
        input::CursorShape shape = input::CursorShape::Default;
        ui::Bitmap image;
        int scale = 1;
        base::Point hotspot;
    };
    void setCursor(std::optional<Cursor> cursor);
    const std::optional<Cursor> &cursor() const { return m_cursor; }
    // Null for a shape.
    compositor::Surface *cursorSurface() const { return m_cursorSurface.get(); }

    // In window coordinates; an empty `shape` is the rounded rect.
    void setBackgroundBlur(const base::Rect &rect, int radius,
                           const render::GlassParams &glass = {}, const ui::Path &shape = {});

    // Its first frame is rendered before the next output frame is composed; a
    // popup whose parent is hidden stays unmapped.
    void show();
    void hide();
    bool isShown() const { return m_shown; }

    std::function<void()> onCloseRequested;
    base::Signal<> mappedChanged;
    std::function<void()> onFrameProduced;
    std::function<void(bool focused)> onKeyboardFocusChanged;

    // Commits the surface even when nothing changed, so other clients'
    // synchronized subsurfaces in it show what they committed.
    void requestFrame() { scheduleRender(); }

    void place();
    base::Rect popupBounds() const;
    ShellWindow *root();
    void release();

    void produceFrame(render::Renderer &renderer, uint64_t frameTimeNs);
    void setKeyboardFocus(bool focused);

    void onWindowActivationChanged(wm::Window *window, bool active) override;
    void onWindowDestroying(wm::Window *window) override;

private:
    // Makes a toolkit window of its own for a null `popup`.
    ShellWindow(Shell *shell, ShellWindow *parent, ui::EmbeddedWindow *popup, wm::WindowRole role);
    void applyMetadata();
    void updateScale();
    void commit();
    void scheduleRender();
    void observeWindow();
    void setPopupBounds(const base::Rect &bounds);
    void updateSizeLimits();
    base::Rect placePopup(const ui::PopupPlacement &placement, const base::Size &size) const;

    Shell *m_shell;
    ShellWindow *m_parent = nullptr;
    std::vector<ShellWindow *> m_popups;
    ui::EmbeddedWindow *m_window = nullptr;
    // Null for a popup, whose window the toolkit owns.
    std::unique_ptr<ui::EmbeddedWindow> m_ownedWindow;
    std::unique_ptr<compositor::Surface> m_surface;
    std::unique_ptr<wm::ShellSurface> m_shellSurface;
    std::unique_ptr<toolkit::PanelTexture> m_texture;
    std::shared_ptr<PanelBuffer> m_buffer;
    std::optional<Cursor> m_cursor;
    std::unique_ptr<compositor::Surface> m_cursorSurface;
    // The surface only refers to its buffer, as to a client's wl_buffer.
    std::shared_ptr<compositor::Buffer> m_cursorBuffer;
    wm::ShellWindowMetadata m_metadata;
    base::Point m_position;
    base::Size m_size;
    double m_scale = 1;
    bool m_shown = false;
    bool m_grab = false;
    bool m_dialog = false;
    bool m_keyboardFocus = false;
    // Inside place(): the geometry it reports may resize the popup.
    bool m_placing = false;
    // Inside produceFrame(): state set while the toolkit renders goes out
    // with the frame's own commit.
    bool m_producing = false;
    wm::Window *m_observed = nullptr;
    base::Subscription m_outputsChanged;
    // False from the top of the destructor, for callbacks the window manager
    // holds.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SHELL_WINDOW_H_
