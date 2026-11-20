// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_IMAGES_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_IMAGES_H_

#include <optional>
#include <string>
#include <vector>

#include "base/dbus_types.h"
#include "ui/bitmap.h"

namespace cutefish::shell {

// Checked where the message arrives; the pixels are converted on a worker.
struct ImageHint {
    int32_t width = 0;
    int32_t height = 0;
    int32_t rowStride = 0;
    bool hasAlpha = false;
    std::string pixels;

    // Nothing for another type, a size past 4096 or fewer bytes than the
    // rows need.
    static std::optional<ImageHint> fromVariant(const base::dbus::Variant &hint);
    // Any thread.
    ui::Bitmap toBitmap() const;
};

// Every function touches the disk and runs on the file thread.
namespace notification_images {

inline constexpr int kThumbnailPixels = 132;

std::string defaultDirectory();
// Returns the path, or empty.
std::string save(const std::string &directory, const ui::Bitmap &image);
void remove(const std::string &directory, const std::string &path);
void prune(const std::string &directory, const std::vector<std::string> &keep);

} // namespace notification_images

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_IMAGES_H_
