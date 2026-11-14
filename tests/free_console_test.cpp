// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session/free_console.h"

#include <gtest/gtest.h>

using cutefish::session::freeConsole;

TEST(FreeConsoleTest, TheLowestTerminalNoSessionIsOn)
{
    // The first two are the display manager's, with a session on them or
    // not: from the greeter the second is still free, but the session needs it.
    EXPECT_EQ(freeConsole({}), 3);
    EXPECT_EQ(freeConsole({1}), 3);
    EXPECT_EQ(freeConsole({1, 2}), 3);
    EXPECT_EQ(freeConsole({1, 2, 3}), 4);
    // A session past the ones logind prompts on does not matter.
    EXPECT_EQ(freeConsole({7}), 3);
    EXPECT_EQ(freeConsole({3, 4, 5, 6}), 0);
}
