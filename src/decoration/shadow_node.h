// Copyright 2012 The Chromium Authors
// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/compositor_extra/shadow.{h,cc}
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_DECORATION_SHADOW_NODE_H_
#define CUTEFISH_DECORATION_SHADOW_NODE_H_

#include "base/geometry.h"
#include "decoration/raster.h"
#include "scene/node.h"
#include "wm/shadow_types.h"
#include "ui/path.h"
#include "wm/window_shadow.h"

namespace cutefish::wm {
class Window;
}

namespace cutefish::decoration {

class Theme;

class ShadowNode : public scene::Node
{
public:
    explicit ShadowNode(const Theme *theme);

    // `content` is in the window node's coordinates; a non-empty `silhouette`,
    // relative to `content`, replaces the corners and radius.
    void update(const base::Rect &content, wm::ShadowElevation elevation, const Corners &corners,
                int radius, const ui::Path &silhouette = {});

    wm::ShadowElevation elevation() const { return m_elevation; }

    Corners corners() const { return m_corners; }

    int radius() const { return m_radius; }

    const ui::Path &silhouette() const { return m_silhouette; }

    base::Insets margins() const { return m_margins; }

    // Where the client surface sits in the window, so the shadow moves with it
    // onto physical pixels.
    void setSurfaceOrigin(const base::Point &origin);

    // Blended over what is behind the window, so the frame has to be
    // composited.
    Content content() const override
    {
        return m_elevation == wm::ShadowElevation::None ? Content::None : Content::Translucent;
    }

protected:
    void paint(render::Renderer &renderer, const base::Rect &bounds) const override;

private:
    const Theme *m_theme = nullptr;
    base::Rect m_content;
    Corners m_corners;
    int m_radius = 0;
    ui::Path m_silhouette;
    base::Point m_surfaceOrigin;
    // The drawn elevation is None whenever the window is too small for the
    // style.
    wm::ShadowElevation m_requested = wm::ShadowElevation::None;
    wm::ShadowElevation m_elevation = wm::ShadowElevation::None;
    base::Insets m_margins;
};

// The node lives under the window's own node, so stacking, hiding and the close
// animation take the shadow along.
class Shadow : public wm::WindowShadow
{
public:
    Shadow(wm::Window *window, Theme *theme);
    Shadow(const Shadow &) = delete;
    Shadow &operator=(const Shadow &) = delete;
    ~Shadow() override;

    void setElevation(wm::ShadowElevation elevation) override;

    wm::ShadowElevation elevation() const override { return m_elevation; }

    void setContentBounds(const base::Rect &bounds) override;

    base::Rect contentBounds() const override { return m_contentBounds; }

    base::Rect shadowBounds() const override;
    void stackAtBottom() override;

    // Null when the window has no scene at all.
    ShadowNode *node() const { return m_node; }

private:
    void updateNode();

    wm::Window *m_window = nullptr;
    Theme *m_theme = nullptr;
    ShadowNode *m_node = nullptr;
    base::Rect m_contentBounds;
    wm::ShadowElevation m_elevation = wm::ShadowElevation::None;
};

class ShadowProvider : public wm::ShadowProvider
{
public:
    explicit ShadowProvider(Theme *theme);

    std::unique_ptr<wm::WindowShadow> createShadow(wm::Window *window) override;

private:
    Theme *m_theme = nullptr;
};

} // namespace cutefish::decoration

#endif // CUTEFISH_DECORATION_SHADOW_NODE_H_
