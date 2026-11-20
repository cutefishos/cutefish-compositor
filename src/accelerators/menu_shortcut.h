// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <string>

#include "input/types.h"

namespace cutefish::accelerators {

class AcceleratorConfiguration;

// The keys the user presses for a menu entry's key equivalent `key` (a
// character or a keysym name) with ui modifiers, as text; empty for a key
// xkb does not know. `terminal` is the menu's window being a terminal,
// `bindsCommand` its program binding the Command key itself.
std::string menuShortcutText(const std::string &key, uint32_t modifiers, input::ShortcutConvention convention,
                             bool terminal, bool bindsCommand, const AcceleratorConfiguration *configuration);

} // namespace cutefish::accelerators
