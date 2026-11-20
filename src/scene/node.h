// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SCENE_NODE_H_
#define CUTEFISH_SCENE_NODE_H_

#include <functional>
#include <memory>
#include <vector>

#include <string>
#include <deque>

#include "render/renderer.h"
#include "base/region.h"

namespace cutefish::scene {

// Geometry is relative to the parent; the first child is furthest back.
class Node
{
public:
    explicit Node(std::string name = {});
    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;
    virtual ~Node();

    const std::string &name() const { return m_name; }

    Node *parent() const { return m_parent; }

    const std::vector<Node *> &children() const { return m_childOrder; }

    Node *addChild(std::unique_ptr<Node> child);
    std::unique_ptr<Node> removeChild(Node *child);

    void raise(Node *child);
    void lower(Node *child);

    // `order` has to list each child once.
    void reorderChildren(const std::vector<Node *> &order);

    void setChildrenBelowParent(size_t count);

    size_t childrenBelowParent() const { return m_childrenBelowParent; }

    base::Rect geometry() const { return m_geometry; }

    void setGeometry(const base::Rect &geometry);

    base::Point position() const { return m_geometry.topLeft(); }

    void setPosition(const base::Point &position) { setGeometry(base::Rect(position, m_geometry.size())); }

    // Position in output coordinates.
    base::Point absolutePosition() const;
    base::Rect absoluteGeometry() const;

    virtual base::Rect absolutePaintBounds() const { return absoluteGeometry(); }

    bool isVisible() const { return m_visible; }

    void setVisible(bool visible);

    void setChangeCallback(std::function<void()> callback) { m_changed = std::move(callback); }

    void notifyChanged() const;
    void notifyDamage(const base::Region &damage);

    void requestFrame() const;

    uint64_t damageId() const { return m_damageId; }

    uint64_t revision() const { return m_revision; }

    static uint64_t mutations() { return s_mutations; }

    uint64_t contentRevision() const { return m_contentRevision; }

    base::Region damageSince(uint64_t revision) const;

    bool isEffectivelyVisible() const;

    virtual void render(render::Renderer &renderer) const;

    enum class Content {
        None,
        Opaque,
        Translucent,
    };

    virtual Content content() const { return Content::None; }

    // Nothing inside can go on a hardware plane.
    virtual bool paintsSubtree() const { return false; }

    virtual bool requiresFullRepaint() const { return paintsSubtree(); }

    virtual bool paintsWhenHidden() const { return false; }

    virtual bool redrawsSubtree(const base::Rect &, double) const { return false; }

    virtual bool acceptsInput(const base::PointF &) const { return true; }

    bool isEventTargetingBlocked() const { return m_eventTargetingBlocked; }

    void setEventTargetingBlocked(bool blocked) { m_eventTargetingBlocked = blocked; }

    virtual bool occludesOutput(const render::Renderer &) const { return false; }

    // False leaves them to the parent.
    virtual bool renderSiblingsBelow(render::Renderer &, size_t /*begin*/, size_t /*end*/) const
    {
        return false;
    }

    bool subtreeOccludesOutput(const render::Renderer &renderer) const;

    bool isPromotedToOverlay() const { return m_promotedToOverlay; }

    void setPromotedToOverlay(bool promoted)
    {
        if (m_promotedToOverlay != promoted)
            ++s_mutations;
        m_promotedToOverlay = promoted;
    }

    bool isExcludedFromCapture() const { return m_excludedFromCapture; }

    void setExcludedFromCapture(bool excluded) { m_excludedFromCapture = excluded; }

    void renderForCapture(render::Renderer &renderer) const;

    virtual bool isRetired() const { return false; }

    void collectRetired();
    virtual void finishRetiring();
    virtual void releaseRenderResources();

protected:
    virtual void paint(render::Renderer &renderer, const base::Rect &absoluteGeometry) const;
    virtual void paintBackdrop(render::Renderer &renderer, const base::Rect &absoluteGeometry) const;
    virtual void paintOverChildrenBelow(render::Renderer &renderer, const base::Rect &absoluteGeometry) const;

    static uint64_t nextDamageId();

    void renderSubtree(render::Renderer &renderer) const;

private:
    // 0 when none does.
    size_t firstUnoccludedChild(const render::Renderer &renderer) const;

    static uint64_t s_mutations;
    const uint64_t m_damageId;
    mutable uint64_t m_revision = 0;
    uint64_t m_contentRevision = 0;
    std::deque<std::pair<uint64_t, base::Region>> m_damageHistory;
    std::function<void()> m_changed;
    std::string m_name;
    Node *m_parent = nullptr;
    std::vector<std::unique_ptr<Node>> m_children;
    std::vector<Node *> m_childOrder;
    base::Rect m_geometry;
    bool m_visible = true;
    bool m_promotedToOverlay = false;
    bool m_eventTargetingBlocked = false;
    bool m_excludedFromCapture = false;
    size_t m_childrenBelowParent = 0;
};

class ColorNode : public Node
{
public:
    explicit ColorNode(const render::Color &color, std::string name = {});

    void setColor(const render::Color &color);

    Content content() const override
    {
        if (m_color.a <= 0.0f)
            return Content::None;
        return m_color.a >= 1.0f ? Content::Opaque : Content::Translucent;
    }

protected:
    void paint(render::Renderer &renderer, const base::Rect &absoluteGeometry) const override;

private:
    render::Color m_color;
};

// Textured quad. The texture is not owned.
class TextureNode : public Node
{
public:
    explicit TextureNode(render::Texture *texture, std::string name = {});

    render::Texture *texture() const { return m_texture; }

    void setTexture(render::Texture *texture)
    {
        m_texture = texture;
        notifyChanged();
    }

    float alpha() const { return m_alpha; }

    void setAlpha(float alpha)
    {
        if (m_alpha != alpha) {
            m_alpha = alpha;
            notifyChanged();
        }
    }

    Content content() const override { return m_texture ? Content::Translucent : Content::None; }

protected:
    void paint(render::Renderer &renderer, const base::Rect &absoluteGeometry) const override;

private:
    render::Texture *m_texture = nullptr;
    float m_alpha = 1.0f;
};

} // namespace cutefish::scene

#endif // CUTEFISH_SCENE_NODE_H_
