// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_DOCK_DOCK_ICON_H_
#define CUTEFISH_SHELL_DOCK_DOCK_ICON_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/timer.h"
#include "ui/bitmap.h"
#include "ui/image.h"
#include "ui/view.h"

namespace cutefish::ui {
class Animation;
class ThemeIcon;
} // namespace cutefish::ui

namespace cutefish::shell {

class CountBadge;
class DockWindow;

class DockIcon : public ui::View
{
public:
    enum class Kind {
        Application,
        Minimized,
        Trash,
    };

    DockIcon(DockWindow *dock, Kind kind);
    ~DockIcon() override;

    Kind kind() const { return m_kind; }
    const std::string &key() const { return m_key; }
    void setKey(std::string key) { m_key = std::move(key); }
    uint64_t windowId() const { return m_windowId; }
    void setWindowId(uint64_t id) { m_windowId = id; }

    void setIcon(std::string name, bool file = false);
    const std::string &iconName() const { return m_iconName; }
    void setToolTip(std::string text) { m_toolTip = std::move(text); }
    const std::string &toolTipText() const { return m_toolTip; }
    void setRunning(bool running);
    bool isRunning() const { return m_running; }
    bool isDotShown() const { return m_running && m_opacity == 1; }
    // Null shows the icon.
    void setThumbnail(const ui::Bitmap *thumbnail, double aspect);
    bool hasThumbnail() const { return !m_thumbnail.isNull(); }
    void setBadgeOpacity(double opacity);
    double badgeOpacity() const { return m_badgeOpacity; }
    // 0 for none.
    void setCount(int64_t count);
    CountBadge *countBadge() const { return m_countBadge; }

    void setIconRect(const ui::RectF &rect);
    ui::RectF iconRect() const { return m_iconRect; }
    // A nonzero `rasterSize` is the artwork's raster size while magnified; 0
    // draws it as at rest.
    void setMagnification(double scale, int rasterSize);
    double magnification() const { return m_magnification; }
    ui::RectF magnifiedIconRect() const;
    void prepareMagnifiedSize(int size);
    // In the parent's coordinates.
    void setRestGeometry(const ui::RectF &slot, const ui::RectF &icon);
    ui::RectF restGeometry() const { return m_restGeometry; }
    ui::RectF restIconRect() const { return m_restIconRect; }
    void setPresentation(double scale, double opacity);
    void setPresentationOpacity(double opacity);
    void bounce();
    bool isBouncing() const;
    // In whole device pixels.
    ui::PointF bounceOffset() const;
    double presentationScale() const { return m_scale; }
    double presentationOpacity() const { return m_opacity; }
    void setDotRect(const ui::RectF &rect);

    bool isDimmed() const { return (m_pressed || m_menuOpen) && !m_dragHidden; }
    bool isPointerOver() const { return m_hovered; }
    void setMenuOpen(bool open);
    void setDraggable(bool draggable) { m_draggable = draggable; }
    void setDragHidden(bool hidden);
    bool isDragHidden() const { return m_dragHidden; }
    void setDropHover(bool hover);
    bool isDropHover() const { return m_dropHover; }
    void releasePress();

    bool acceptsPointer() const override { return true; }

protected:
    void pointerEnter() override;
    void pointerLeave() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerMove(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;
    void paint(ui::Painter &painter) override;

private:
    class Bounce;
    class SvgFace;
    class ThumbnailFace;
    class ThumbnailMask;

    void paintThumbnail(ui::Painter &painter, const ui::RectF &slot);
    ui::RectF thumbnailRect(const ui::RectF &slot) const;
    ui::RectF artworkRect() const;
    ui::Transform artworkTransform(const ui::RectF &rect, double scale) const;
    void updateFace();
    void updateMask();
    void applyBadgeOpacity();
    void placeCountBadge();
    void setLift(double lift);

    DockWindow *m_dock;
    const Kind m_kind;
    std::string m_key;
    uint64_t m_windowId = 0;
    std::string m_iconName;
    bool m_iconIsFile = false;
    std::string m_toolTip;
    bool m_running = false;
    ui::ThemeIcon *m_themeFace = nullptr;
    SvgFace *m_svgFace = nullptr;
    ui::ThemeIcon *m_themeMask = nullptr;
    SvgFace *m_svgMask = nullptr;
    ui::ThemeIcon *m_badge = nullptr;
    ThumbnailFace *m_thumbnailFace = nullptr;
    ThumbnailMask *m_thumbnailMask = nullptr;
    ui::Image m_thumbnail;
    double m_thumbnailAspect = 1;
    double m_badgeOpacity = 1;
    double m_badgeFrom = 1;
    double m_badgeTarget = 1;
    bool m_badgeSet = false;
    std::unique_ptr<ui::Animation> m_badgeFade;
    CountBadge *m_countBadge = nullptr;
    ui::RectF m_iconRect;
    ui::RectF m_dotRect;
    ui::RectF m_restGeometry;
    ui::RectF m_restIconRect;
    double m_magnification = 1;
    int m_rasterSize = 0;
    std::string m_preparedName;
    int m_preparedSize = 0;
    double m_preparedScale = 0;
    double m_scale = 1;
    double m_opacity = 1;
    std::unique_ptr<Bounce> m_bounce;
    // Of the bounce height, 0 at rest.
    double m_lift = 0;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_menuOpen = false;
    bool m_draggable = false;
    bool m_dragArmed = false;
    ui::PointF m_pressPoint;
    bool m_dragHidden = false;
    bool m_dropHover = false;
    int m_pressedButton = -1;
    base::Timer m_holdTimer;
    bool m_held = false;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_DOCK_DOCK_ICON_H_
