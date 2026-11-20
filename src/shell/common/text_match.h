// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_TEXT_MATCH_H_
#define CUTEFISH_SHELL_COMMON_TEXT_MATCH_H_

#include <string>
#include <string_view>

namespace cutefish::shell {

bool containsIgnoringCase(std::string_view text, std::string_view needle);

std::string toLower(std::string_view text);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_TEXT_MATCH_H_
