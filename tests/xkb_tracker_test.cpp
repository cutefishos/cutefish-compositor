// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Cutefish counterpart to the XkbTracker half of
// chromium/src/components/exo/keyboard_unittest.cc.
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#include <gtest/gtest.h>

#include <vector>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

#include "input/xkb_tracker.h"

using namespace cutefish::input;

TEST(XkbTrackerTest, TheDefaultKeymapCompilesAndIsSerializable)
{
    XkbTracker tracker;
    const std::string keymap = tracker.keymap();

    EXPECT_FALSE(keymap.empty());
    EXPECT_NE(std::string::npos, keymap.find("xkb_keymap"));
    EXPECT_NE(std::string::npos, keymap.find("xkb_symbols"));
}

TEST(XkbTrackerTest, KeysymsFollowTheEvdevOffset)
{
    XkbTracker tracker;
    EXPECT_EQ(XKB_KEY_a, tracker.keysym(KEY_A));
    EXPECT_EQ(XKB_KEY_1, tracker.keysym(KEY_1));
}

TEST(XkbTrackerTest, ShiftChangesTheEffectiveKeysymAndTheModifierMask)
{
    XkbTracker tracker;
    EXPECT_EQ(0u, tracker.modifiers().depressed);

    tracker.updateKey(KEY_LEFTSHIFT, true);
    const KeyboardModifiers pressed = tracker.modifiers();
    EXPECT_NE(0u, pressed.depressed);
    EXPECT_EQ(XKB_KEY_A, tracker.keysym(KEY_A));

    tracker.updateKey(KEY_LEFTSHIFT, false);
    EXPECT_EQ(0u, tracker.modifiers().depressed);
    EXPECT_EQ(XKB_KEY_a, tracker.keysym(KEY_A));
}

TEST(XkbTrackerTest, CapsLockLatchesIntoTheLockedMask)
{
    XkbTracker tracker;
    tracker.updateKey(KEY_CAPSLOCK, true);
    tracker.updateKey(KEY_CAPSLOCK, false);

    const KeyboardModifiers modifiers = tracker.modifiers();
    EXPECT_NE(0u, modifiers.locked);
    EXPECT_EQ(0u, modifiers.depressed);
    EXPECT_EQ(XKB_KEY_A, tracker.keysym(KEY_A));
}

TEST(XkbTrackerTest, NumLockStartsOn)
{
    XkbTracker tracker;
    EXPECT_TRUE(tracker.modifierActive(XKB_MOD_NAME_NUM));
    EXPECT_EQ(KeyboardLed::NumLock, tracker.leds());
    EXPECT_EQ(XKB_KEY_KP_1, tracker.keysym(KEY_KP1));
    EXPECT_EQ(0u, tracker.modifiers().depressed);

    tracker.updateKey(KEY_NUMLOCK, true);
    tracker.updateKey(KEY_NUMLOCK, false);
    EXPECT_FALSE(tracker.modifierActive(XKB_MOD_NAME_NUM));
    EXPECT_EQ(XKB_KEY_KP_End, tracker.keysym(KEY_KP1));
}

TEST(XkbTrackerTest, TheLockLightsFollowTheLocks)
{
    XkbTracker tracker;
    EXPECT_EQ(KeyboardLed::NumLock, tracker.leds());

    tracker.updateKey(KEY_CAPSLOCK, true);
    tracker.updateKey(KEY_CAPSLOCK, false);
    EXPECT_EQ(KeyboardLed::CapsLock | KeyboardLed::NumLock, tracker.leds());
    tracker.updateKey(KEY_NUMLOCK, true);
    tracker.updateKey(KEY_NUMLOCK, false);
    EXPECT_EQ(KeyboardLed::CapsLock, tracker.leds());

    ASSERT_TRUE(tracker.updateKeyboardLayout("de"));
    EXPECT_EQ(KeyboardLed::CapsLock, tracker.leds());
    tracker.updateKey(KEY_NUMLOCK, true);
    tracker.updateKey(KEY_NUMLOCK, false);
    EXPECT_EQ(KeyboardLed::CapsLock | KeyboardLed::NumLock, tracker.leds());
    tracker.updateKey(KEY_CAPSLOCK, true);
    tracker.updateKey(KEY_CAPSLOCK, false);
    EXPECT_EQ(KeyboardLed::NumLock, tracker.leds());
}

TEST(XkbTrackerTest, CopyingLocksKeepsTheHeldModifiers)
{
    XkbTracker source;
    XkbTracker tracker;
    source.updateKey(KEY_CAPSLOCK, true);
    source.updateKey(KEY_CAPSLOCK, false);
    tracker.updateKey(KEY_LEFTSHIFT, true);

    EXPECT_TRUE(tracker.copyLocks(source));
    EXPECT_FALSE(tracker.copyLocks(source));
    EXPECT_EQ(KeyboardLed::CapsLock | KeyboardLed::NumLock, tracker.leds());
    EXPECT_TRUE(tracker.modifierPressed(XKB_MOD_NAME_SHIFT));

    tracker.updateKey(KEY_LEFTSHIFT, false);
    EXPECT_FALSE(tracker.modifierPressed(XKB_MOD_NAME_SHIFT));
    EXPECT_EQ(KeyboardLed::CapsLock | KeyboardLed::NumLock, tracker.leds());
}

TEST(XkbTrackerTest, ANamedLayoutReplacesTheKeymap)
{
    XkbTracker tracker;
    const std::string original = tracker.keymap();

    ASSERT_TRUE(tracker.updateKeyboardLayout("de"));
    const std::string german = tracker.keymap();
    EXPECT_NE(original, german);
    EXPECT_EQ(XKB_KEY_z, tracker.keysym(KEY_Y));
}

TEST(XkbTrackerTest, ALayoutChangeKeepsTheLocksAndAnEmptyNameRestoresTheDefault)
{
    XkbTracker tracker;
    const std::string original = tracker.keymap();
    tracker.updateKey(KEY_CAPSLOCK, true);
    tracker.updateKey(KEY_CAPSLOCK, false);
    tracker.updateKey(KEY_LEFTSHIFT, true);

    ASSERT_TRUE(tracker.updateKeyboardLayout("de"));
    EXPECT_EQ("de", tracker.layoutName());
    EXPECT_TRUE(tracker.modifierActive(XKB_MOD_NAME_CAPS));
    EXPECT_FALSE(tracker.modifierPressed(XKB_MOD_NAME_SHIFT));

    ASSERT_TRUE(tracker.updateKeyboardLayout(""));
    EXPECT_TRUE(tracker.layoutName().empty());
    EXPECT_EQ(original, tracker.keymap());
    EXPECT_TRUE(tracker.modifierActive(XKB_MOD_NAME_CAPS));
}

TEST(XkbTrackerTest, AnUncompilableLayoutLeavesThePreviousKeymapInPlace)
{
    XkbTracker tracker;
    ASSERT_TRUE(tracker.updateKeyboardLayout("de"));
    const std::string german = tracker.keymap();

    EXPECT_FALSE(tracker.updateKeyboardLayout("definitely-not-a-layout"));
    EXPECT_EQ(german, tracker.keymap());
    EXPECT_EQ(XKB_KEY_z, tracker.keysym(KEY_Y));
}

TEST(XkbTrackerTest, AKeyStrokeUsesTheFewestModifiers)
{
    XkbTracker tracker;
    const auto plain = tracker.keyStroke(XKB_KEY_a);
    ASSERT_TRUE(plain);
    EXPECT_EQ(uint32_t(KEY_A), plain->key);
    EXPECT_TRUE(plain->modifiers.empty());

    const auto shifted = tracker.keyStroke(XKB_KEY_exclam);
    ASSERT_TRUE(shifted);
    EXPECT_EQ(uint32_t(KEY_1), shifted->key);
    EXPECT_EQ(std::vector<uint32_t> {KEY_LEFTSHIFT}, shifted->modifiers);

    EXPECT_FALSE(tracker.keyStroke(XKB_KEY_Greek_alpha));
    EXPECT_FALSE(tracker.keyStroke(XKB_KEY_NoSymbol));
}

TEST(XkbTrackerTest, AKeyStrokeFollowsTheLayoutAndItsAltGr)
{
    XkbTracker tracker;
    ASSERT_TRUE(tracker.updateKeyboardLayout("de"));

    const auto z = tracker.keyStroke(XKB_KEY_z);
    ASSERT_TRUE(z);
    EXPECT_EQ(uint32_t(KEY_Y), z->key);

    const auto at = tracker.keyStroke(XKB_KEY_at);
    ASSERT_TRUE(at);
    EXPECT_EQ(uint32_t(KEY_Q), at->key);
    EXPECT_EQ(std::vector<uint32_t> {KEY_RIGHTALT}, at->modifiers);
}

TEST(XkbTrackerTest, AKeyStrokeAccountsForCapsLock)
{
    XkbTracker tracker;
    tracker.updateKey(KEY_CAPSLOCK, true);
    tracker.updateKey(KEY_CAPSLOCK, false);

    const auto upper = tracker.keyStroke(XKB_KEY_A);
    ASSERT_TRUE(upper);
    EXPECT_EQ(uint32_t(KEY_A), upper->key);
    EXPECT_TRUE(upper->modifiers.empty());
}
