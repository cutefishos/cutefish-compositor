// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <string_view>

namespace cutefish::display {

// Empty when the block is missing, has no EDID header or fails its checksum.
struct EdidInfo {
    // SHA-256 over the vendor/product/serial bytes and the serial string.
    std::string key;
    // The three-letter PNP manufacturer ID, such as "DEL".
    std::string make;
    // The monitor name descriptor; panels often leave it out.
    std::string model;
};

EdidInfo parseEdid(std::string_view edid);

} // namespace cutefish::display
