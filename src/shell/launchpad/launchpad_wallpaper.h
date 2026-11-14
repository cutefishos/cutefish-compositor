// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_WALLPAPER_H_
#define CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_WALLPAPER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/geometry.h"
#include "base/signal.h"
#include "base/thread_pool.h"
#include "ui/geometry.h"
#include "ui/image.h"

namespace cutefish::ui {
class Bitmap;
class Painter;
} // namespace cutefish::ui

namespace cutefish::shell {

class Wallpaper;

// What the launchpad draws behind its grid: the desktop's decode of the
// wallpaper, shrunk to an eighth of the surface, blurred by three box passes
// and shown stretched; or the solid background colour.
class LaunchpadWallpaper
{
public:
    // The fraction of the surface the picture is shrunk to.
    static constexpr double kQuality = 0.125;

    // Follows `wallpaper`'s background and shares its decode; null shows black.
    explicit LaunchpadWallpaper(Wallpaper *wallpaper);
    ~LaunchpadWallpaper();

    LaunchpadWallpaper(const LaunchpadWallpaper &) = delete;
    LaunchpadWallpaper &operator=(const LaunchpadWallpaper &) = delete;

    // The surface's size in device pixels; a change shrinks again.
    void setSurfaceSize(const base::Size &size);
    // Takes the background again, and shrinks what changed.
    void refresh();

    bool isLoaded() const { return m_loaded; }
    // A picture (BackgroundType 0), drawn with the dimmer over it, or the colour.
    bool isPicture() const { return m_picture; }
    // Null until shrunk, or when the file cannot be read.
    const ui::Image &image() const { return m_image; }
    ui::Color color() const { return m_color; }
    // What the icons sit on: the colour, or the blurred picture's average
    // under the dimmer; black while unknown.
    ui::Color backdropColor() const;
    // The picture under the dimmer over `bounds`, or the colour. Until the
    // picture has decoded, the dimmer darkens what is behind.
    void paint(ui::Painter &painter, const ui::RectF &bounds) const;

    base::Signal<> changed;

    // The provider's blur: three horizontal and vertical box passes of
    // radius 3, in place on premultiplied 0xAARRGGBB pixels.
    static void blur(ui::Bitmap &image);

private:
    struct Result;

    // On a worker: the decode cropped to `target`, blurred.
    static Result *shrink(const ui::Bitmap &decode, const base::Size &target);
    void decoded(const std::string &key, const ui::Bitmap &image);

    Wallpaper *m_wallpaper;
    base::Subscription m_backgroundChanged;
    base::Size m_size;
    std::shared_ptr<void> m_request;
    base::Job m_job;
    bool m_loaded = false;
    bool m_picture = true;
    ui::Image m_image;
    ui::Color m_color {0, 0, 0, 255};
    std::optional<ui::Color> m_average;
    // The picture and size shown, and the one on its way.
    std::string m_key;
    std::string m_pendingKey;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_WALLPAPER_H_
