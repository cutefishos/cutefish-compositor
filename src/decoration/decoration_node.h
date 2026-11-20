// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WindowVisualNode owns the shared body clip and final exterior outline.

#ifndef CUTEFISH_DECORATION_DECORATION_NODE_H_
#define CUTEFISH_DECORATION_DECORATION_NODE_H_

#include <memory>
#include <vector>

#include <string>

#include "scene/node.h"

namespace cutefish::decoration {

class Decoration;
class Theme;

class DecorationNode : public scene::Node
{
public:
    DecorationNode(const Decoration* decoration, Theme* theme);
    ~DecorationNode() override;

    // The client area inside this geometry is deliberately left unpainted.
    Content content() const override { return Content::Translucent; }

    void releaseRenderResources() override;

protected:
    void paint(render::Renderer& renderer, const base::Rect& geometry) const override;

private:
    void paintFrame(render::Renderer& renderer, const base::Rect& bounds) const;
    void paintCaption(render::Renderer& renderer, const base::Rect& bounds) const;
    void paintButtons(render::Renderer& renderer, const base::Rect& bounds) const;

    const Decoration* m_decoration = nullptr;
    Theme* m_theme = nullptr;

    // Kept once per output scale, so outputs at different scales do not rebuild
    // it for each other.
    struct Caption {
        double scale = 0;
        std::string key;
        std::unique_ptr<render::Texture> texture;
    };
    mutable std::vector<Caption> m_captions;
};

} // namespace cutefish::decoration

#endif // CUTEFISH_DECORATION_DECORATION_NODE_H_
