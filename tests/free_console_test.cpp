// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session/free_console.h"

#include <gtest/gtest.h>

using cutefish::session::freeConsole;

TEST(FreeConsoleTest, TheLowestTerminalNoSessionIsOn)
{
    // The first is the display manager's, with a session on it or not.
    EXPECT_EQ(freeConsole({}), 2);
    EXPECT_EQ(freeConsole({1}), 2);
    // A session on the second, as after signing in from the greeter.
    EXPECT_EQ(freeConsole({2}), 3);
    EXPECT_EQ(freeConsole({1, 2, 4}), 3);
    // A session past the ones logind prompts on does not matter.
    EXPECT_EQ(freeConsole({7}), 2);
    EXPECT_EQ(freeConsole({2, 3, 4, 5, 6}), 0);
}
