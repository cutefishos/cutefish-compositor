// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once


#include "render/damage.h"
#include "render/gl_program.h"
#include "base/geometry.h"

namespace cutefish::render {

// `sigma` and `reach` are in texels; `stepX` and `stepY` are one texel in
// texture coordinates. `program` must be in use.
void setGaussianKernel(const GlProgram &program, float sigma, float stepX, float stepY,
                       int reach = kGaussianMaxReach);

// Rows count bottom-up, as GL lays them.
void setGaussianClamp(const GlProgram &program, const base::Rect &texels, const base::Size &size);

} // namespace cutefish::render
