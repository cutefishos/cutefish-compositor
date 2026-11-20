// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/common/file_theme_icon.h"

#include <cmath>
#include <map>
#include <set>

#include "ui/painter.h"
#include "ui/theme.h"

namespace cutefish::shell {

namespace {

struct FileIcons
{
    std::set<std::string> loaded;
    std::map<std::string, std::vector<std::function<void()>>> loading;
};

FileIcons &fileIcons()
{
    static FileIcons icons;
    return icons;
}

std::string keyFor(const std::string &name, int size, double scale)
{
    return ui::Theme::current().iconTheme + "/" + name + "@" + std::to_string(size) + "x"
            + std::to_string(std::lround(size * scale));
}

// True when the raster is in the icon cache; otherwise `ready` runs once it is.
bool load(const std::string &name, int size, double scale, std::function<void()> ready)
{
    FileIcons &icons = fileIcons();
    const std::string key = keyFor(name, size, scale);
    if (icons.loaded.contains(key))
        return true;
    if (!icons.loading.contains(key)) {
        icons.loading[key];
        ui::preloadThemeIcons({name}, size, scale, [key] {
            FileIcons &icons = fileIcons();
            icons.loaded.insert(key);
            const auto found = icons.loading.find(key);
            if (found == icons.loading.end())
                return;
            std::vector<std::function<void()>> waiting = std::move(found->second);
            icons.loading.erase(found);
            for (const std::function<void()> &callback : waiting)
                callback();
        });
        if (icons.loaded.contains(key))
            return true;
    }
    if (ready)
        icons.loading[key].push_back(std::move(ready));
    return false;
}

} // namespace

FileThemeIcon::FileThemeIcon(std::string name, double size)
    : ui::ThemeIcon(std::move(name), size)
{
}

FileThemeIcon::~FileThemeIcon() = default;

void FileThemeIcon::preload(std::vector<std::string> names, int size, double scale, std::function<void()> done)
{
    std::vector<std::string> themed;
    std::vector<std::string> files;
    for (std::string &name : names)
        (name.starts_with('/') ? files : themed).push_back(std::move(name));
    auto remaining = std::make_shared<size_t>(files.size() + 1);
    auto finished = [remaining, done = std::move(done)] {
        if (--*remaining == 0 && done)
            done();
    };
    for (const std::string &name : files) {
        if (load(name, size, scale, finished))
            finished();
    }
    ui::preloadThemeIcons(std::move(themed), size, scale, finished);
}

bool FileThemeIcon::isReady(const std::string &name, int size, double scale, std::function<void()> ready)
{
    return !name.starts_with('/') || load(name, size, scale, std::move(ready));
}

void FileThemeIcon::paint(ui::Painter &painter)
{
    std::weak_ptr<bool> alive = m_alive;
    if (!isReady(iconName(), int(sizeHint().width), painter.scale(), [this, alive] {
            if (!alive.expired())
                requestPaint();
        }))
        return;
    ui::ThemeIcon::paint(painter);
}

} // namespace cutefish::shell
