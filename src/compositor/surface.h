// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/surface.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_COMPOSITOR_SURFACE_H_
#define CUTEFISH_COMPOSITOR_SURFACE_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/region.h"
#include "base/transform.h"

#include "compositor/buffer.h"
#include "compositor/menu_model.h"
#include "render/glass.h"
#include "render/presentation.h"
#include "ui/path.h"

namespace cutefish::render {

class DrmSyncobj;
class Renderer;
} // namespace cutefish::render

namespace cutefish::scene {

class Node;
class SurfaceNode;
} // namespace cutefish::scene

namespace cutefish::compositor {

class Compositor;
class SubSurface;
class SurfaceObserver;

// Counter-clockwise rotations, matching wl_output.transform.
enum class Transform {
    Normal,
    Rotate90,
    Rotate180,
    Rotate270,
    Flipped,
    FlippedRotate90,
    FlippedRotate180,
    FlippedRotate270,
};

enum class WindowLevel {
    Normal,
    Floating,
    AboveDock,
};

enum class ResizeAnchor {
    TopLeft,
    Center,
};

enum class PlacementEdge {
    None,
    Top,
    Bottom,
    Left,
    Right,
    TopLeft,
    BottomLeft,
    TopRight,
    BottomRight,
};

// `rect` is in global logical coordinates.
struct PlacementAnchor {
    base::Rect rect;
    PlacementEdge anchor = PlacementEdge::None;
    PlacementEdge gravity = PlacementEdge::None;

    bool operator==(const PlacementAnchor &) const = default;
};

// A missing time destroys the wl_callback silently.
using FrameCallback = std::function<void(std::optional<uint32_t> presentationTimeMs)>;

// Pending state goes to cached at commit() and to current at
// commitSurfaceHierarchy(); an unset attachment means attach() was not called
// since the last commit.
class Surface
{
public:
    explicit Surface(Compositor *compositor);
    Surface(const Surface &) = delete;
    Surface &operator=(const Surface &) = delete;
    ~Surface();

    bool skipTaskbar() const { return m_skipTaskbar; }
    void setSkipTaskbar(bool skip);
    bool alwaysOnTop() const { return m_level != WindowLevel::Normal; }
    void setAlwaysOnTop(bool alwaysOnTop);
    WindowLevel level() const { return m_level; }
    void setLevel(WindowLevel level);
    ResizeAnchor resizeAnchor() const { return m_resizeAnchor; }
    void setResizeAnchor(ResizeAnchor anchor) { m_resizeAnchor = anchor; }
    bool serverResize() const { return m_serverResize; }
    void setServerResize(bool enabled) { m_serverResize = enabled; }
    const std::string &restoreId() const { return m_restoreId; }
    void setRestoreId(const std::string &id) { m_restoreId = id; }
    const base::Size &restoreSize() const { return m_restoreSize; }
    void setRestoreSize(const base::Size &size) { m_restoreSize = size; }
    const std::optional<PlacementAnchor> &placementAnchor() const { return m_placementAnchor; }
    void setPlacementAnchor(const std::optional<PlacementAnchor> &anchor) { m_placementAnchor = anchor; }
    // In global coordinates.
    const std::optional<base::Point> &requestedPosition() const { return m_requestedPosition; }
    void setRequestedPosition(const std::optional<base::Point> &position) { m_requestedPosition = position; }
    // Empty for its rectangle.
    const ui::Path &bodyShape() const { return m_bodyShape; }
    void setBodyShape(const ui::Path &shape);
    bool castsShadow() const { return m_castsShadow; }
    void setCastsShadow(bool castsShadow);
    // 0 to 1.
    float opacity() const { return m_opacity; }
    void setOpacity(float opacity);
    bool hasFullscreenBars() const { return m_hasFullscreenBars; }
    int fullscreenBarsHeight() const { return m_fullscreenBarsHeight; }
    bool fullscreenBarsPush() const { return m_fullscreenBarsPush; }
    void setFullscreenBars(int height, bool push)
    {
        m_hasFullscreenBars = true;
        m_fullscreenBarsHeight = height;
        m_fullscreenBarsPush = push;
    }

    struct MenuAddress {
        std::string service;
        std::string path;
        bool operator==(const MenuAddress &) const = default;
    };

    // Menu metadata is immediate and independent of buffer commits.
    uint64_t addMenuSource();
    void setMenuAddress(uint64_t source, const MenuAddress &address);
    void removeMenuSource(uint64_t source);

    const MenuAddress &menuAddress() const { return m_menuAddress; }

    const GtkMenuAddress &gtkMenuAddress() const { return m_gtkMenuAddress; }
    void setGtkMenuAddress(const GtkMenuAddress &address);

    // Null for none; takes precedence over the menu address.
    const std::shared_ptr<MenuModel> &mainMenu() const { return m_mainMenu; }
    void setMainMenu(std::shared_ptr<MenuModel> menu);
    const std::shared_ptr<MenuModel> &dockMenu() const { return m_dockMenu; }
    void setDockMenu(std::shared_ptr<MenuModel> menu);

    void attach(const std::shared_ptr<Buffer> &buffer, const base::Point &offset);
    void damage(const base::Rect &rect);
    void damageBuffer(const base::Rect &rect);
    void requestFrameCallback(FrameCallback callback);
    void requestPresentationCallback(render::PresentationContent::Callback callback);
    void abandonUnsubmittedPresentation(const render::PresentationSubmission &submission);

    void setBackgroundBlur(const base::Rect &rect, int radius);

    base::Rect backgroundBlurRect() const { return m_state.basicState.blurRect; }

    int backgroundBlurRadius() const { return m_state.basicState.blurRadius; }

    // Surface-local; empty for the rounded rectangle.
    void setBackgroundBlurShape(const ui::Path &shape)
    {
        m_pending.basicState.blurShape = shape;
    }

    // Empty when there is none, no rectangle, or glass.
    ui::Path backgroundBlurShape() const;

    void setGlass(const render::GlassParams &glass) { m_pending.basicState.glass = glass; }

    // Requests within a commit accumulate.
    const render::GlassParams &pendingGlass() const { return m_pending.basicState.glass; }

    const render::GlassParams &glass() const { return m_state.basicState.glass; }

    // Drawn over the subsurfaces placed below this one, before its own content.
    void setSubsurfaceBlur(const base::Rect &rect, int radius)
    {
        m_pending.basicState.subsurfaceBlurRect = rect;
        m_pending.basicState.subsurfaceBlurRadius = radius;
    }

    base::Rect subsurfaceBlurRect() const { return m_state.basicState.subsurfaceBlurRect; }
    int subsurfaceBlurRadius() const { return m_state.basicState.subsurfaceBlurRadius; }

    // Surface-local; empty for the rounded rectangle.
    void setSubsurfaceBlurShape(const ui::Path &shape) { m_pending.basicState.subsurfaceBlurShape = shape; }

    // The fill fields of the parameters only.
    void setSubsurfaceBlurFill(const render::GlassParams &fill) { m_pending.basicState.subsurfaceBlurFill = fill; }
    const render::GlassParams &subsurfaceBlurFill() const { return m_state.basicState.subsurfaceBlurFill; }

    // Empty when there is none or no rectangle.
    ui::Path subsurfaceBlurShape() const;

    void setOpaqueRegion(const base::Region &region);
    void setInputRegion(const base::Region &region);
    void resetInputRegion();

    void setCrop(std::optional<base::RectF> crop) { m_pending.basicState.crop = crop; }

    void setViewport(std::optional<base::Size> viewport)
    {
        m_pending.basicState.viewport = viewport;
    }

    std::optional<base::RectF> crop() const { return m_state.basicState.crop; }

    std::optional<base::Size> viewport() const { return m_state.basicState.viewport; }

    uint32_t preferredScale() const;
    void notifyOutputScaleChanged();

    bool hasFractionalScale() const { return m_hasFractionalScale; }

    void setHasFractionalScale(bool value) { m_hasFractionalScale = value; }

    bool hasViewport() const { return m_hasViewport; }

    void setHasViewport(bool value) { m_hasViewport = value; }
    enum class ViewportError {
        None,
        BadSize,
        OutOfBuffer
    };
    ViewportError cachedViewportError() const;
    base::AffineTransform surfaceToBuffer() const;
    base::AffineTransform textureMapping() const;

    void setBufferScale(int scale);
    void setClientScale(double scale);
    void setBufferTransform(Transform transform);

    // Double-buffered and applied to the buffer attached in the same commit.
    void setAcquireFence(render::SyncFence fence);
    void setAcquirePoint(std::shared_ptr<render::DrmSyncobj> timeline, uint64_t point);
    void setBufferRelease(std::shared_ptr<BufferRelease> release);

    // Inside onSurfaceCommit() this is the buffer being applied; a synchronized
    // subsurface caches its buffer without applying it.
    bool lastCommitAttachedBuffer() const { return m_lastCommitAttachedBuffer; }

    bool lastCommitBufferSupportsExplicitSync() const
    {
        return m_lastCommitBufferSupportsExplicitSync;
    }

    // What it shows was drawn for where it used to be.
    bool isMovedAheadOfContent() const { return m_movedAheadOfContent; }

    void commit();

    size_t heldCommitCount() const { return m_heldCommits.size(); }

    bool isSynchronized() const;
    Surface *parentSurface() const;

    bool isMapped() const;
    void commitSurfaceHierarchy(bool synchronized);
    // From the root down, so every ancestor's hit test region takes the change
    // in.
    void commitDesynchronized();

    // SubSurface sets this itself; other role owners call setHasRole().
    bool hasRole() const { return m_subSurface != nullptr || m_hasRole; }

    void setHasRole(bool hasRole) { m_hasRole = hasRole; }

    void addSurfaceObserver(SurfaceObserver *observer);
    void removeSurfaceObserver(SurfaceObserver *observer);
    bool hasSurfaceObserver(const SurfaceObserver *observer) const;

    // Bottom to top, positions relative to this surface; the entry for `this`
    // marks the parent's own content.
    const std::vector<std::pair<Surface *, base::Point>> &subSurfaceStack() const
    {
        return m_subSurfaceStack;
    }

    // Excludes subsurfaces.
    bool hitTest(const base::Point &point) const { return m_localHitTestRegion.contains(point); }

    base::Region localHitTestRegion() const { return m_localHitTestRegion; }

    // In surface-local coordinates.
    base::Region hitTestRegion() const { return m_hitTestRegion; }

    bool hasPendingAttachedBuffer() const;

    base::Region pendingDamage() const { return m_pending.damage; }

    size_t pendingFrameCallbackCount() const { return m_pending.frameCallbacks.size(); }

    std::shared_ptr<Buffer> buffer() const;
    // -1x-1 without a buffer.
    base::Size bufferSize() const;

    base::Point offset() const { return m_state.basicState.offset; }

    int bufferScale() const { return m_state.basicState.bufferScale; }

    double clientScale() const { return m_state.basicState.clientScale; }

    Transform bufferTransform() const { return m_state.basicState.bufferTransform; }

    base::Region opaqueRegion() const { return m_state.basicState.opaqueRegion; }

    std::optional<base::Region> inputRegion() const { return m_state.basicState.inputRegion; }

    // In surface-local coordinates.
    base::Region damageRegion() const;

    // -1x-1 without a buffer.
    base::Size contentSize() const;
    // For the cached state, which a synchronized subsurface shows once its
    // parent commits.
    base::Size cachedContentSize() const;

    bool hasContents() const { return !bufferSize().isEmpty(); }

    bool isFullyOpaque() const;

    size_t committedFrameCallbackCount() const { return m_state.frameCallbacks.size(); }

    size_t inFlightFrameCallbackCount() const { return m_inFlightFrameCallbacks.size(); }

    bool awaitsFrame() const;

    // Returns whether that changed since the last call.
    bool updatePlacement();

    void setExternalInputRegion(std::optional<base::Region> region);

    scene::SurfaceNode *node() const { return m_node; }

    base::Point position() const { return m_position; }

    // A minimized window: kept with its buffer, state and role, but not
    // composited or hit tested.
    bool isHidden() const { return m_hidden; }

    void setHidden(bool hidden);

    void setPosition(const base::Point &position);

    // A null `newParent` detaches the node from the scene.
    void reparentNode(scene::Node *newParent);

    // Returns true if anything was uploaded.
    bool updateTexture(render::Renderer &renderer);

    // Nonzero generations include only mapped, effectively visible surfaces.
    void frameSubmitted(uint64_t generation = 0);
    void frameDiscarded(uint64_t generation);

    std::shared_ptr<Surface *> lifetimeToken() const { return m_lifetime; }

    void frameDrawn(uint32_t timeMs, uint64_t generation = 0);

private:
    friend class SubSurface;
    struct CommitRequest;

    void addSubSurface(Surface *child);
    void removeSubSurface(Surface *child);
    // False when none is held and the mode can change now.
    bool holdCommitBehavior(bool synchronized);
    void applyCommit(CommitRequest &request);
    void holdCommit(std::unique_ptr<CommitRequest> request);
    void applyReadyCommits();
    Surface *rootSurface();
    Surface *oldestHeldCommit();
    void setSubSurfacePosition(Surface *child, const base::Point &position);
    bool placeSubSurface(Surface *child, Surface *reference, bool above);
    void applyCachedState();
    void updateSubSurfaceStacking();
    void updateMapping();
    void updateHitTestRegion();
    void notifyWindowHintsChanged();

    struct SubSurfaceEntry {
        Surface *surface;
        base::Point position;
    };

    using SubSurfaceList = std::list<SubSurfaceEntry>;
    // The parent is a sentinel entry, permitting children on either side.
    SubSurfaceList m_pendingSubSurfaces;
    SubSurfaceList m_cachedSubSurfaces;
    SubSurfaceList m_subSurfaces;
    SubSurface *m_subSurface = nullptr; // Owned by the wl_resource.
    bool m_hadSubSurfaceRole = false;
    bool m_hasRole = false;
    bool m_hasFractionalScale = false;
    bool m_hasViewport = false;
    std::unique_ptr<scene::Node> m_detachedNode;
    bool m_needsCommitSurface = false;

    struct State {
        std::optional<base::RectF> crop;
        std::optional<base::Size> viewport;
        base::Rect blurRect;
        int blurRadius = 0;
        ui::Path blurShape;
        render::GlassParams glass;
        base::Rect subsurfaceBlurRect;
        int subsurfaceBlurRadius = 0;
        ui::Path subsurfaceBlurShape;
        render::GlassParams subsurfaceBlurFill;
        base::Region opaqueRegion;
        // Unset means the whole surface accepts input, not an empty region.
        std::optional<base::Region> inputRegion;
        int bufferScale = 1;
        // Only Xwayland sets it: a buffer of scale 1 maps onto size /
        // clientScale logical units.
        double clientScale = 1;
        Transform bufferTransform = Transform::Normal;
        base::Point offset;
    };

    // Weak, so a client destroying its committed wl_buffer cannot dangle, while
    // the size is kept.
    struct BufferAttachment {
        std::weak_ptr<Buffer> buffer;
        base::Size size;
    };

    struct ExtendedState {
        State basicState;
        // Unset in the pending state means attach() was not called since the
        // last commit.
        std::optional<BufferAttachment> buffer;
        // Not persisted between commits.
        base::Region damage;
        base::Region bufferDamage;
        bool fullDamage = false;
        std::vector<FrameCallback> frameCallbacks;
        // Not persisted and never copied: they belong to exactly one buffer
        // attachment.
        render::SyncFence acquireFence;
        std::shared_ptr<render::DrmSyncobj> acquireTimeline;
        uint64_t acquirePoint = 0;
        std::shared_ptr<BufferRelease> release;
    };

    static base::Size contentSizeOf(const ExtendedState &state);
    base::Rect bufferRectToSurface(const base::Rect &rect) const;
    base::Rect surfaceRectToBuffer(const base::Rect &rect) const;

    Compositor *m_compositor = nullptr;
    // Owned by its parent in the scene tree; detached in the destructor.
    scene::SurfaceNode *m_node = nullptr;
    std::optional<base::Region> m_externalInputRegion;
    base::Point m_position;
    bool m_hidden = false;

    std::shared_ptr<render::PresentationContent> m_pendingPresentation;
    std::shared_ptr<render::PresentationContent> m_cachedPresentation;
    std::vector<std::weak_ptr<render::PresentationContent>> m_presentations;
    ExtendedState m_pending;
    ExtendedState m_cached;
    ExtendedState m_state;

    std::shared_ptr<Buffer> m_uploadBuffer;
    bool m_textureDirty = false;
    // In buffer pixels.
    base::Region m_uploadDamage;
    bool m_uploadAll = true;
    bool m_lastCommitAttachedBuffer = false;
    bool m_movedAheadOfContent = false;
    bool m_lastCommitBufferSupportsExplicitSync = false;

    std::vector<FrameCallback> m_inFlightFrameCallbacks;
    uint64_t m_frameGeneration = 0;

    struct Placement {
        base::Rect bounds;
        bool shown = false;

        bool operator==(const Placement &) const = default;
    };

    Placement m_placement;
    void updateMenuAddress();
    void notifyMenuChanged();
    std::map<uint64_t, MenuAddress> m_menuSources;
    uint64_t m_nextMenuSource = 0;
    MenuAddress m_menuAddress;
    GtkMenuAddress m_gtkMenuAddress;
    std::shared_ptr<MenuModel> m_mainMenu;
    std::shared_ptr<MenuModel> m_dockMenu;
    bool m_skipTaskbar = false;
    WindowLevel m_level = WindowLevel::Normal;
    bool m_castsShadow = true;
    float m_opacity = 1;
    bool m_hasFullscreenBars = false;
    int m_fullscreenBarsHeight = 0;
    bool m_fullscreenBarsPush = false;
    ResizeAnchor m_resizeAnchor = ResizeAnchor::TopLeft;
    bool m_serverResize = false;
    std::string m_restoreId;
    // -1x-1 until the client sets one.
    base::Size m_restoreSize {-1, -1};
    std::optional<PlacementAnchor> m_placementAnchor;
    std::optional<base::Point> m_requestedPosition;
    ui::Path m_bodyShape;

    std::shared_ptr<Surface *> m_lifetime = std::make_shared<Surface *>(this);

    base::Region m_localHitTestRegion;
    base::Region m_hitTestRegion;
    std::vector<std::pair<Surface *, base::Point>> m_subSurfaceStack;

    std::vector<SurfaceObserver *> m_observers;

    std::deque<std::unique_ptr<CommitRequest>> m_heldCommits;
};

} // namespace cutefish::compositor

#endif // CUTEFISH_COMPOSITOR_SURFACE_H_
