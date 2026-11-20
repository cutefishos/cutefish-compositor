// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "compositor/output.h"
#include <string>

#include "base/geometry.h"

#include "base/signal.h"

namespace cutefish::scene {
class Node;
}

namespace cutefish::compositor {

// The owner keeps the fields current and emits changed() when the buffer
// constraints move or the source goes away.
class CaptureSource final
{
public:
    enum class Kind {
        Output,
        Window,
    };

    Kind kind = Kind::Output;
    Output output;
    // Window sources only; bounds are logical, in global coordinates.
    const scene::Node *node = nullptr;
    base::Rect bounds;
    double scale = 1;
    std::string outputName;
    bool active = true;

    base::Size pixelSize() const
    {
        if (kind == Kind::Output)
            return output.pixelSize;
        return base::Size(base::roundToInt(bounds.width() * scale),
                          base::roundToInt(bounds.height() * scale));
    }

    std::string renderedOn() const { return kind == Kind::Output ? output.name : outputName; }

    base::Signal<> changed;
};

} // namespace cutefish::compositor
