// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SESSION_SECURE_CONTAINER_H_
#define CUTEFISH_SESSION_SECURE_CONTAINER_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/file_watcher.h"
#include "base/geometry.h"
#include "base/signal.h"
#include "ui/bitmap.h"

namespace cutefish::render {
class Renderer;
}

namespace cutefish::scene {
class ColorNode;
class Node;
} // namespace cutefish::scene

namespace cutefish::session {

struct SecureOutput {
    std::string name;
    base::Rect bounds;
    // Device pixels per logical pixel, for content drawn at the output's density.
    double scale = 1;
    bool primary = false;

    friend bool operator==(const SecureOutput &, const SecureOutput &) = default;
};

struct CoverSample {
    float backdrop = 1;
    float panelOpacity = 1;
    double panelScale = 1;
};

class SecureContainer
{
public:
    static constexpr int kCoverDurationMs = 360;
    static constexpr int kUncoverDurationMs = 300;

    explicit SecureContainer(scene::Node *root);
    SecureContainer(const SecureContainer &) = delete;
    SecureContainer &operator=(const SecureContainer &) = delete;
    ~SecureContainer();

    scene::Node *node() const { return m_root; }

    scene::Node *authContainer() const { return m_auth; }

    scene::Node *shieldContainer() const { return m_shield; }

    void setBlocked(bool blocked);

    // Without `animate` it is there at once.
    void setCovered(bool covered, bool animate);

    // For a lock taken as the displays go dark: they must wake to the lock screen.
    void finishTransition();

    // In frame-clock milliseconds. The first frame drawn starts it.
    CoverSample sample(double frameTime, double framePeriod) const;
    CoverSample sample(const render::Renderer &renderer) const;

    // A lock is not in place until such a frame is on screen.
    bool isSettled(double frameTime) const;

    void requestAnimationFrame() const;

    void setOutputs(const std::vector<SecureOutput> &outputs);

    const std::vector<SecureOutput> &outputs() const { return m_outputs; }

    // Including wallpapers that could not be read.
    bool isWallpaperReady() const;

    // At its device size; null while none has loaded.
    ui::Bitmap wallpaperImage(const std::string &output) const;
    // Possibly before its node is in place.
    base::Signal<> wallpaperLoaded;

    // `done` runs while the returned handle is kept; unset, the lock screen decodes on
    // its own.
    using PictureLoader = std::function<std::shared_ptr<void>(
            const std::string &path, const base::Size &deviceSize, std::function<void(const ui::Bitmap &)> done)>;
    void setPictureLoader(PictureLoader loader) { m_pictureLoader = std::move(loader); }
    const PictureLoader &pictureLoader() const { return m_pictureLoader; }
    // Empty for the plain colour.
    const std::string &picture() const { return m_picture; }

    // At the login screen, the copy cutefish-dm keeps for the account picked.
    void setSettingsFile(const std::string &path, bool crossfade);
    const std::string &settingsFile() const { return m_settingsFile; }

private:
    void rebuildShield(bool crossfade = false);
    void refreshWallpaper(bool crossfade = false);
    std::string m_settingsFile;
    std::string m_watchedSettings;
    std::string m_watchedWallpaper;
    base::FileWatch m_settingsWatch;
    base::FileWatch m_wallpaperWatch;
    std::string m_wallpaperKey;
    std::string m_picture;
    PictureLoader m_pictureLoader;

    int transitionDuration() const;

    scene::Node *m_root = nullptr;
    scene::Node *m_shield = nullptr;
    scene::Node *m_auth = nullptr;
    std::vector<scene::ColorNode *> m_shieldRects;
    std::vector<SecureOutput> m_outputs;
    bool m_blocked = false;
    bool m_covered = true;
    bool m_animating = false;
    double m_requested = 0;
    mutable std::optional<double> m_start;
};

} // namespace cutefish::session

#endif // CUTEFISH_SESSION_SECURE_CONTAINER_H_
