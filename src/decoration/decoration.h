// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_DECORATION_DECORATION_H_
#define CUTEFISH_DECORATION_DECORATION_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/geometry.h"

#include "decoration/caption_button.h"
#include "decoration/decoration_theme.h"
#include "wm/window_frame.h"
#include "wm/window_observer.h"

namespace cutefish::wm {
class Window;
}

namespace cutefish::decoration {

class DecorationNode;

class Decoration : public wm::WindowFrame, public wm::WindowObserver, public ThemeObserver
{
public:
    Decoration(wm::Window* window, Theme* theme);
    Decoration(const Decoration&) = delete;
    Decoration& operator=(const Decoration&) = delete;
    ~Decoration() override;

    wm::Window* window() const { return m_window; }

    bool isActive() const;
    std::string title() const;
    const std::vector<CaptionButton>& buttons() const { return m_buttons; }
    // In frame coordinates.
    base::Rect captionArea() const;
    bool isRestoreButton() const;

    wm::Insets extents() const override;
    base::Rect inputBounds() const override;
    wm::FrameArea areaAt(const base::PointF& location) const override;
    input::CursorShape cursorShapeAt(const base::PointF& location) const override;
    bool onPointerMotion(input::Timestamp timestamp, const base::PointF& location) override;
    bool onPointerButton(input::Timestamp timestamp, const base::PointF& location,
                         bool pressed) override;
    void onPointerLeave() override;
    bool isTrackingPress() const override { return m_pressedButton >= 0 && !m_pressTouch; }
    void cancelPress() override;
    base::Rect titleBarBounds() const override;
    std::vector<base::Rect> captionButtonBounds() const override;
    bool onTouchDown(input::Timestamp timestamp, int32_t id, const base::PointF& location) override;
    bool onTouchMotion(input::Timestamp timestamp, int32_t id, const base::PointF& location) override;
    bool onTouchUp(input::Timestamp timestamp, int32_t id) override;
    void onTouchCancel() override;

    void onWindowBoundsChanged(wm::Window* window, const base::Rect& oldBounds) override;
    void onWindowContentChanged(wm::Window* window) override
    { onWindowBoundsChanged(window, base::Rect()); }
    void onWindowFrameChanged(wm::Window* window) override
    { onWindowBoundsChanged(window, base::Rect()); }
    void onWindowStateTypeChanged(wm::Window* window, wm::WindowStateType oldType) override;
    void onWindowActivationChanged(wm::Window* window, bool active) override;
    void onWindowTitleChanged(wm::Window* window) override;
    void onWindowDestroying(wm::Window* window) override;

    void onThemeChanged() override
    {
        layout();
        scheduleRepaint();
    }

private:
    // Frame coordinates: the outer window bounds with their origin at (0, 0).
    base::Point toFrame(const base::PointF& location) const;
    base::Rect frameRect() const;
    base::Rect titleBarRect() const;
    void layout();
    void updateNode();
    void scheduleRepaint();
    // -1 when the point is not on a button.
    int buttonIndexAt(const base::Point& framePoint) const;
    wm::ResizeEdge resizeEdgeAt(const base::Point& framePoint) const;
    bool press(input::Timestamp timestamp, const base::PointF& location,
               std::optional<int32_t> touchId);
    void releaseButton(const base::Point& framePoint);
    void runButtonAction(CaptionButtonType type);
    bool setHoveredButton(int index);
    // Gaps included, in frame coordinates.
    base::Rect controlsRect() const;
    void toggleMaximize();

    wm::Window* m_window = nullptr;
    Theme* m_theme = nullptr;
    DecorationNode* m_node = nullptr;

    std::vector<CaptionButton> m_buttons;
    int m_hoveredButton = -1;
    int m_pressedButton = -1;
    // Empty while the pointer holds it.
    std::optional<int32_t> m_pressTouch;
    base::Point m_pressTouchPoint;

    input::Timestamp m_lastTitleBarPress {};
    base::Point m_lastTitleBarPressPoint;
    bool m_hadTitleBarPress = false;
};

class DecorationProvider : public wm::FrameProvider
{
public:
    explicit DecorationProvider(Theme* theme);

    std::unique_ptr<wm::WindowFrame> createFrame(wm::Window* window) override;
    wm::Insets defaultExtents() const override;

private:
    Theme* m_theme = nullptr;
};

} // namespace cutefish::decoration

#endif // CUTEFISH_DECORATION_DECORATION_H_
