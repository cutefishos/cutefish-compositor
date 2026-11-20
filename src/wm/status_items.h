// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_STATUS_ITEMS_H_
#define CUTEFISH_WM_STATUS_ITEMS_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "compositor/surface_observer.h"

namespace cutefish::compositor {
class SubSurface;
class Surface;
} // namespace cutefish::compositor

namespace cutefish::wm {

class ShellSurface;
class StatusItems;
class WindowManager;

// While placed, a synchronized subsurface of the bar's surface.
class StatusItem : public compositor::SurfaceObserver
{
public:
    struct Client {
        // Returns the serial to acknowledge.
        std::function<uint32_t(int height, int maxWidth)> configure;
        // `foreground` is 0xAARRGGBB with straight alpha.
        std::function<void(bool dark, uint32_t foreground)> appearance;
        std::function<void(bool highlighted)> highlight;
        // Global logical; empty while the item has no place.
        std::function<void(const base::Rect &rect)> placed;
    };

    StatusItem(StatusItems *items, compositor::Surface *surface, std::string owner, Client client);
    StatusItem(const StatusItem &) = delete;
    StatusItem &operator=(const StatusItem &) = delete;
    ~StatusItem() override;

    // Null once the surface is gone.
    compositor::Surface *surface() const { return m_surface; }
    const std::string &owner() const { return m_owner; }

    const std::string &identifier() const { return m_identifier; }
    void setIdentifier(std::string identifier);

    void setHighlightRequested(bool requested);
    bool isHighlighted() const { return m_highlighted; }

    // False for a serial never sent or already acknowledged.
    bool acknowledgeConfigure(uint32_t serial);
    bool isConfigured() const { return m_configured; }

    // The committed content's width, logical; 0 without a buffer.
    int width() const { return m_width; }
    bool isShown() const;

    // `bounds` is relative to the bar. False while the item has no place.
    bool adoptPopup(ShellSurface *popup, const base::Rect &bounds);
    ShellSurface *popupParent() const;

    // `position` is in the bar's surface coordinates.
    void place(const base::Point &position);
    void unplace();
    bool isPlaced() const { return m_subSurface != nullptr; }
    base::Point position() const { return m_position; }
    // Global logical; empty while unplaced.
    base::Rect screenRect() const;

    void onSurfaceDestroying(compositor::Surface *surface) override;
    void onSurfaceCommit(compositor::Surface *surface) override;

    void registryGone();
    void configure();
    void sendAppearance();
    void updateHighlight();
    void sendPlaced();

private:
    void releaseSurface();
    void dismissPopups();

    StatusItems *m_items;
    compositor::Surface *m_surface;
    std::string m_owner;
    Client m_client;
    std::string m_identifier;
    std::unique_ptr<compositor::SubSurface> m_subSurface;
    base::Point m_position;
    std::deque<uint32_t> m_pendingSerials;
    bool m_configured = false;
    int m_width = 0;
    bool m_highlightRequested = false;
    bool m_highlighted = false;
    bool m_highlightSent = false;
    base::Rect m_sentRect;
    bool m_rectSent = false;
};

class StatusItems
{
public:
    explicit StatusItems(WindowManager *manager);
    StatusItems(const StatusItems &) = delete;
    StatusItems &operator=(const StatusItems &) = delete;
    // The items outlive it inert, out of any bar.
    ~StatusItems();

    WindowManager *manager() const { return m_manager; }

    // In the bar's order, leftmost first: the newest first.
    const std::vector<StatusItem *> &items() const { return m_items; }

    // Null when there is no bar.
    void setBar(compositor::Surface *surface, ShellSurface *shellSurface);
    compositor::Surface *barSurface() const { return m_barSurface; }
    ShellSurface *barShellSurface() const { return m_barShellSurface; }
    // The bar surface's position on the screen, global logical.
    void setBarOrigin(const base::Point &origin);
    base::Point barOrigin() const { return m_barOrigin; }
    void setLimits(int height, int maxWidth);
    int height() const { return m_height; }
    int maxWidth() const { return m_maxWidth; }
    void setAppearance(bool dark, uint32_t foreground);
    bool darkAppearance() const { return m_dark; }
    uint32_t foreground() const { return m_foreground; }

    base::Signal<> changed;
    base::Signal<> committed;

    void add(StatusItem *item);
    void remove(StatusItem *item);
    void identifierSet(StatusItem *item);
    void itemChanged() { changed(); }
    void contentCommitted() { committed(); }

private:
    WindowManager *m_manager;
    std::vector<StatusItem *> m_items;
    std::map<std::string, size_t> m_rememberedPlaces;
    compositor::Surface *m_barSurface = nullptr;
    ShellSurface *m_barShellSurface = nullptr;
    base::Point m_barOrigin;
    int m_height = 0;
    int m_maxWidth = 0;
    bool m_dark = false;
    uint32_t m_foreground = 0xd9000000;
    base::Subscription m_popupGrabsChanged;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_STATUS_ITEMS_H_
