// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_CORE_PROCESS_ENVIRONMENT_H_
#define CUTEFISH_CORE_PROCESS_ENVIRONMENT_H_

#include <string>
#include <vector>

namespace cutefish {

std::vector<std::string> childEnvironment();

} // namespace cutefish

#endif // CUTEFISH_CORE_PROCESS_ENVIRONMENT_H_
