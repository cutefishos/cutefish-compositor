// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "base/geometry.h"
#include "input/cursor_shape.h"
#include "ui/bitmap.h"

namespace cutefish::cursor {

struct Frame {
    // Argb32Premultiplied.
    ui::Bitmap image;
    base::Point hotspot;
    int delayMs = 0;
};

std::string resourceName(input::CursorShape shape);
// The images of the nominal size closest to `size`, the larger on a tie;
// `nominal` gets that size.
std::vector<Frame> readXCursor(std::string_view bytes, int size, int *nominal = nullptr);
// `provider` gets the theme, `theme` or one it inherits, whose files the
// images came from; it is left alone for the built-in arrow.
std::vector<Frame> load(input::CursorShape shape, int size, const std::string &theme = "default",
                        int *nominal = nullptr, std::string *provider = nullptr);
std::vector<std::string> themePaths();
// Replaces the search path; an empty list restores the standard one.
void setThemePathsForTesting(std::vector<std::string> paths);
bool validThemeName(std::string_view theme);
int defaultSize();

} // namespace cutefish::cursor
