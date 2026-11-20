// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_FILE_THEME_ICON_H_
#define CUTEFISH_SHELL_COMMON_FILE_THEME_ICON_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/icon.h"

namespace cutefish::shell {

// A theme icon that never reads an icon file named by its path on this
// thread: an application bundle's icon may sit on a mounted disk image, whose
// reads can hang. The file is read and rasterised on the pool, and nothing is
// drawn until it is there. Theme names draw as ThemeIcon draws them.
class FileThemeIcon : public ui::ThemeIcon
{
public:
    explicit FileThemeIcon(std::string name = {}, double size = 32);
    ~FileThemeIcon() override;

    // ui::preloadThemeIcons(), after which these icons draw at once at that
    // size and scale.
    static void preload(std::vector<std::string> names, int size, double scale, std::function<void()> done = {});
    // Whether the icon at that size and scale draws now; when not, it is
    // loading and `ready` runs here once it is in the icon cache.
    static bool isReady(const std::string &name, int size, double scale, std::function<void()> ready);

protected:
    void paint(ui::Painter &painter) override;

private:
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_FILE_THEME_ICON_H_
