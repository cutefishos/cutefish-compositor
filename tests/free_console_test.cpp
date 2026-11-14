// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "session/free_console.h"

#include <gtest/gtest.h>

using cutefish::session::displayManagerConsoles;
using cutefish::session::freeConsole;
using cutefish::session::mayEnterConsole;

TEST(FreeConsoleTest, TheDisplayManagersTerminalsComeFromItsVariable)
{
    EXPECT_EQ(displayManagerConsoles("1,2"), (std::set<int> {1, 2}));
    EXPECT_EQ(displayManagerConsoles(" 7 , 8 "), (std::set<int> {7, 8}));
    EXPECT_TRUE(displayManagerConsoles(nullptr).empty());
    EXPECT_TRUE(displayManagerConsoles("").empty());
    EXPECT_EQ(displayManagerConsoles("1,x,-3,2"), (std::set<int> {1, 2}));
}

TEST(FreeConsoleTest, ADisplayManagersTerminalOnlyWithASessionOnIt)
{
    const std::set<int> displayManager {1, 2};
    // From the greeter, the sessions' terminal is empty: staying is the answer.
    EXPECT_FALSE(mayEnterConsole(2, displayManager, {1}));
    // Signed in, the greeter has gone from the first.
    EXPECT_FALSE(mayEnterConsole(1, displayManager, {2}));
    // Back to a session or a greeter that is there.
    EXPECT_TRUE(mayEnterConsole(2, displayManager, {1, 2}));
    EXPECT_TRUE(mayEnterConsole(1, displayManager, {1, 2}));
    // Any other terminal, a console or not.
    EXPECT_TRUE(mayEnterConsole(3, displayManager, {2}));
    EXPECT_TRUE(mayEnterConsole(9, displayManager, {}));
    // Not started by cutefish-dm: nothing is kept from.
    EXPECT_TRUE(mayEnterConsole(1, {}, {}));
}

TEST(FreeConsoleTest, TheLowestTerminalNoSessionIsOn)
{
    const std::set<int> displayManager {1, 2};
    EXPECT_EQ(freeConsole({}, displayManager), 3);
    EXPECT_EQ(freeConsole({1, 2}, displayManager), 3);
    EXPECT_EQ(freeConsole({1, 2, 3}, displayManager), 4);
    // A session past the ones logind prompts on does not matter.
    EXPECT_EQ(freeConsole({7}, displayManager), 3);
    EXPECT_EQ(freeConsole({3, 4, 5, 6}, displayManager), 0);
    // Another display manager's terminals, as cutefish-dm can be set to.
    EXPECT_EQ(freeConsole({}, {7, 8}), 1);
    EXPECT_EQ(freeConsole({1, 2}, {7, 8}), 3);
    // Without the list, the first two are left alone.
    EXPECT_EQ(freeConsole({}, {}), 3);
}
