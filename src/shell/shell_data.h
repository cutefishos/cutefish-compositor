// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SHELL_DATA_H_
#define CUTEFISH_SHELL_SHELL_DATA_H_

#include <string>
#include <string_view>

#include "render/glass.h"

namespace cutefish::shell {

std::string shellDataFile(std::string_view name);
void setShellDataDirectoryForTesting(std::string directory);

// Every value goes through wl_fixed, as cutefish_surface_blur_v1 requests carry
// it.
float glassFixed(double value);

render::GlassParams fishuiGlass();
// `tint` is premultiplied, from rgba components rounded to 16 bits each.
render::GlassParams popupGlass(double red, double green, double blue, double alpha);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SHELL_DATA_H_
