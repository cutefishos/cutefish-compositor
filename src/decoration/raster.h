// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_DECORATION_RASTER_H_
#define CUTEFISH_DECORATION_RASTER_H_

#include <cstdint>
#include <vector>

#include "base/geometry.h"

#include "render/renderer.h"

namespace cutefish::decoration {

// Premultiplied RGBA8888, which is what the renderer's texture path blends.
class Image
{
public:
    explicit Image(const base::Size& size);

    base::Size size() const { return m_size; }
    int stride() const { return m_size.width() * 4; }
    const uint8_t* bits() const { return m_pixels.data(); }

    // `coverage` is 0..1.
    void blend(int x, int y, const render::Color& color, float coverage);

private:
    base::Size m_size;
    std::vector<uint8_t> m_pixels;
};

struct Corners {
    bool topLeft = true;
    bool topRight = true;
    bool bottomLeft = true;
    bool bottomRight = true;

    static Corners top() { return Corners {true, true, false, false}; }
    static Corners all() { return Corners(); }
};

void fillRoundedRect(Image& image, const base::RectF& rect, double radius, const render::Color& color,
                     const Corners& corners = Corners::all());

void fillContinuousRoundedRect(Image& image, const base::RectF& rect, double radius,
                               const render::Color& color, const Corners& corners = Corners::all());

} // namespace cutefish::decoration

#endif // CUTEFISH_DECORATION_RASTER_H_
