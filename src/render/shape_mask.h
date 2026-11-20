// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "base/geometry.h"
#include "render/renderer.h"
#include "ui/bitmap.h"
#include "ui/path.h"

namespace cutefish::render {

ui::Color toUiColor(const Color &color);

std::string shapeKey(const ui::Path &path);

// A logical path placed on physical pixels: pixel = point * scale + offset.
struct ShapeRaster {
    double scale = 1;
    base::PointF offset;
    base::Size size;

    // `margin` is in physical pixels; the image origin stays on a physical
    // pixel.
    static ShapeRaster covering(const base::RectF &logical, double scale, int margin = 0);

    // In logical output coordinates.
    base::RectF target() const;

    std::string key() const;

    bool operator==(const ShapeRaster &other) const
    {
        return scale == other.scale && base::fuzzyCompare(offset, other.offset) && size == other.size;
    }
};

// Premultiplied RGBA8888 white, so the alpha is the coverage.
ui::Bitmap rasterizeShape(const ui::Path &path, const ShapeRaster &raster);

// `widthPx` is in physical pixels.
ui::Bitmap rasterizeShapeOutline(const ui::Path &path, const ShapeRaster &raster, double widthPx,
                                 const Color &outline, const Color &highlight);

} // namespace cutefish::render
