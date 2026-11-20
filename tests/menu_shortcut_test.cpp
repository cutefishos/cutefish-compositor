// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "accelerators/accelerator_configuration.h"
#include "accelerators/menu_shortcut.h"
#include "input/types.h"
#include "input_test_util.h"
#include "ui/event.h"

using namespace cutefish;
using namespace cutefish::accelerators;

namespace {

constexpr uint32_t kControl = ui::ControlModifier;
constexpr uint32_t kControlAlt = ui::ControlModifier | ui::AltModifier;

} // namespace

TEST(MenuShortcut, CompositorCommandShortcutsArePressedWithControl)
{
    input_test::TemporaryDirectory directory;
    AcceleratorConfiguration configuration(directory.filePath("compositor.conf"));
    configuration.setConvention(input::ShortcutConvention::Command);
    const auto command = input::ShortcutConvention::Command;

    EXPECT_EQ("Command+N", menuShortcutText("n", kControl, command, false, false, &configuration));
    EXPECT_EQ("Command+Q", menuShortcutText("q", kControl, command, false, false, &configuration));
    EXPECT_EQ("Ctrl+H", menuShortcutText("h", kControl, command, false, false, &configuration));
    EXPECT_EQ("Ctrl+Alt+H", menuShortcutText("h", kControlAlt, command, false, false, &configuration));
    EXPECT_EQ("Ctrl+M", menuShortcutText("m", kControl, command, false, false, &configuration));
    EXPECT_EQ("Ctrl+H", menuShortcutText("h", kControl, input::ShortcutConvention::Pc, false, false,
                                         &configuration));
}

TEST(MenuShortcut, ProgramsBindingCommandKeepCommandForTheCompositorsShortcuts)
{
    input_test::TemporaryDirectory directory;
    AcceleratorConfiguration configuration(directory.filePath("compositor.conf"));
    configuration.setConvention(input::ShortcutConvention::Command);
    const auto command = input::ShortcutConvention::Command;

    EXPECT_EQ("Command+H", menuShortcutText("h", kControl, command, false, true, &configuration));
    EXPECT_EQ("Command+Alt+H", menuShortcutText("h", kControlAlt, command, false, true, &configuration));
    EXPECT_EQ("Command+M", menuShortcutText("m", kControl, command, false, true, &configuration));
    EXPECT_EQ("Command+Q", menuShortcutText("q", kControl, command, false, true, &configuration));
    EXPECT_EQ("Command+N", menuShortcutText("n", kControl, command, false, true, &configuration));
}
