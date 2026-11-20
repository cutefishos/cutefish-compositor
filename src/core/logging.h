// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_CORE_LOGGING_H_
#define CUTEFISH_CORE_LOGGING_H_

#include <string_view>

#include "base/logging.h"

namespace cutefish::core {

using LogCategory = base::LogCategory;

// Applies `rules` (base::logging::setRules syntax) to every category.
void setLoggingRules(std::string_view rules);

} // namespace cutefish::core

extern const cutefish::core::LogCategory cfWayland;
extern const cutefish::core::LogCategory cfSession;
extern const cutefish::core::LogCategory cfDrm;
extern const cutefish::core::LogCategory cfRender;
extern const cutefish::core::LogCategory cfScene;
extern const cutefish::core::LogCategory cfCompositor;
extern const cutefish::core::LogCategory cfInput;
extern const cutefish::core::LogCategory cfInputTransitions;
extern const cutefish::core::LogCategory cfCursorTransitions;

#endif // CUTEFISH_CORE_LOGGING_H_
