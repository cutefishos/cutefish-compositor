// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RENDER_CURSOR_SINK_H_
#define CUTEFISH_RENDER_CURSOR_SINK_H_


#include <cstdint>
#include "base/geometry.h"

namespace cutefish::render {

// Premultiplied RGBA8888 in increasing address order, matching the compositor's
// own texture upload format.
struct CursorImage {
    // Premultiplied ARGB32, which is the plane's ARGB8888 in memory.
    const uint8_t* pixels = nullptr;
    base::Size size;
    int stride = 0;
    base::Point hotspot;
};

// Every method may fail; the cursor then goes back into the scene.
class CursorSink
{
public:
    virtual ~CursorSink() = default;

    // False when the sink cannot show this image; the caller must composite it.
    virtual bool setCursorImage(const CursorImage& image) = 0;

    virtual void hideCursor() = 0;

    // Logical desktop coordinates for the cursor's own sink, output pixels for
    // an output.
    virtual void moveCursor(const base::PointF& position) = 0;
};

} // namespace cutefish::render

#endif // CUTEFISH_RENDER_CURSOR_SINK_H_
