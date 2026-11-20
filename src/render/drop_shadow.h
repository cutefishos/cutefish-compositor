// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "render/gl_program.h"
#include "render/renderer.h"
#include "base/geometry.h"

namespace cutefish::render {

// Each lobe is a separable Gaussian of the caster's coverage, on a copy halved
// until its sigma fits the kernel.
class DropShadow
{
public:
    bool initialize();

    // Needs a raster scale of 1, so logical units are image pixels, and no
    // rounded clip set.
    std::unique_ptr<Texture> render(Renderer &renderer, const base::Size &size,
                                    const std::vector<ShadowLobe> &lobes,
                                    const std::function<void()> &paintCaster);

private:
    // Rows bottom-up; `extent` is the texture coordinate the far corner
    // samples.
    void draw(Texture *texture, const base::RectF &rect, const base::Size &viewport,
              const base::PointF &extent = {1, 1}) const;

    GlProgram m_program;
};

} // namespace cutefish::render
