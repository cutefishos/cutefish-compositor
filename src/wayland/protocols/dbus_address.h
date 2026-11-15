// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string_view>

namespace cutefish::wayland {

// A unique name (":1.42") or a well-known one ("org.example.Menu").
bool validBusName(std::string_view name);
bool validObjectPath(std::string_view path);

} // namespace cutefish::wayland
