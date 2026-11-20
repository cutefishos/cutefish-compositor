// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SCENE_SCENE_H_
#define CUTEFISH_SCENE_SCENE_H_

#include <memory>

#include "scene/damage_tracker.h"
#include "scene/node.h"
#include "base/geometry.h"

namespace cutefish::scene {

class Scene
{
public:
    Scene();
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;
    ~Scene();

    Node *root() { return m_root.get(); }

    const Node *root() const { return m_root.get(); }

    void setSize(const base::Size &size);

    base::Size size() const { return m_root->geometry().size(); }

    void render(render::Renderer &renderer) const;

    bool hasDamage(const render::OutputRenderContext &context) const;

    bool requiresFullRepaint(const base::Rect &viewport) const;

    void retireClosing();

    render::Color background() const { return m_background; }

    void setBackground(const render::Color &color)
    {
        m_background = color;
        m_root->notifyChanged();
    }

private:
    base::Rect viewportFor(const render::OutputRenderContext &context) const;

    mutable std::map<std::string, DamageTracker> m_damageTrackers;
    std::unique_ptr<Node> m_root;
    render::Color m_background;
};

} // namespace cutefish::scene

#endif // CUTEFISH_SCENE_SCENE_H_
