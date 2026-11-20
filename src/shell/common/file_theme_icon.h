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

// Reads icon files on the pool: a bundle's icon may sit on a mounted disk image
// whose reads can hang.
class FileThemeIcon : public ui::ThemeIcon
{
public:
    explicit FileThemeIcon(std::string name = {}, double size = 32);
    ~FileThemeIcon() override;

    static void preload(std::vector<std::string> names, int size, double scale, std::function<void()> done = {});
    // When false, `ready` runs here once the icon is in the cache.
    static bool isReady(const std::string &name, int size, double scale, std::function<void()> ready);

protected:
    void paint(ui::Painter &painter) override;

private:
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_FILE_THEME_ICON_H_
