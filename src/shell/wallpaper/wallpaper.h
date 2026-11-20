// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_WALLPAPER_WALLPAPER_H_
#define CUTEFISH_SHELL_WALLPAPER_WALLPAPER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "base/thread_pool.h"
#include "shell/shell.h"
#include "shell/wallpaper/bar_backdrop.h"
#include "shell/wallpaper/wallpaper_cache.h"
#include "ui/bitmap.h"
#include "ui/geometry.h"

namespace cutefish::appearance {
class Appearance;
}

namespace cutefish::compositor {
class CaptureSource;
}

namespace cutefish::scene {
class Node;
class WallpaperNode;
}

namespace cutefish::wm {
class Window;
}

namespace cutefish::shell {

// A new picture replaces the old one only once it is decoded and, on the primary
// output, judged for the bar, so the picture, the gradient and the bar's contents
// change in the same frame.
class Wallpaper final : public Component
{
public:
    struct Background {
        // BackgroundType: 0 a picture, otherwise the colour.
        int type = 0;
        std::string path;
        ui::Color color {0x2b, 0x8a, 0xda, 255};

        bool operator==(const Background &) const = default;
    };

    // Empty `cacheDirectory` is the user's; empty `servicesSettings` reads the user's
    // services.conf.
    explicit Wallpaper(Shell *shell, std::string cacheDirectory = {}, std::string servicesSettings = {});
    ~Wallpaper() override;

    // Until the service answers, services.conf names the background.
    void followAppearance(appearance::Appearance *appearance);
    void setBackground(const Background &background);
    const Background &background() const { return m_background; }
    base::Signal<> backgroundChanged;

    WallpaperCache &cache() { return m_cache; }

    // Invalid while nothing is judged.
    bool isJudged() const { return m_judged; }
    const BarBackdrop::Style &style() const { return m_style; }
    // The judgement changed, in the same loop turn as the picture it is for.
    base::Signal<> judgementChanged;

    // Null for a colour or nothing.
    ui::Bitmap primaryPicture() const;

    bool isReady() const;

    scene::WallpaperNode *node(const std::string &output) const;
    std::vector<const scene::Node *> nodes() const;

    std::shared_ptr<compositor::CaptureSource> desktopCaptureSource(wm::Window *desktop, const std::string &output);

private:
    struct Screen;
    class CaptureNode;

    void updateOutputs();
    void reload(Screen &screen);
    void tryCommit(Screen &screen);
    void decoded(Screen &screen, const ui::Bitmap &image);
    void updateShownPictures();
    void present(const Screen &screen);
    void publish(bool judged, const BarBackdrop::Style &style);
    void updateGradient();
    void readServicesSettings();
    void reloadAppearance();
    static std::string sampleKey(const std::string &path, const base::Size &size);

    Shell *m_shell;
    WallpaperCache m_cache;
    std::string m_servicesSettings;
    Background m_background;
    bool m_hasBackground = false;
    appearance::Appearance *m_appearance = nullptr;
    std::unique_ptr<appearance::Appearance> m_ownAppearance;
    base::Subscription m_appearanceChanged;
    base::Job m_settingsJob;
    std::vector<std::unique_ptr<Screen>> m_screens;
    std::map<std::string, BarBackdrop::Sample> m_samples;
    bool m_judged = false;
    BarBackdrop::Style m_style;
    base::Subscription m_outputsChanged;
    base::Subscription m_themeChanged;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_WALLPAPER_WALLPAPER_H_
