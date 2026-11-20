// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_MIME_ICONS_H_
#define CUTEFISH_SHELL_COMMON_MIME_ICONS_H_

#include <string>

namespace cutefish::shell {

// Touches the disk: call it on the file thread or a worker.
std::string mimeIconNameForFile(const std::string &path);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_MIME_ICONS_H_
