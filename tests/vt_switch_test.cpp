// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h>
#include <linux/input-event-codes.h>
#include <memory>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "input/event_sink.h"
#include "input/input_manager.h"
#include "input/keyboard.h"
#include "input/keyboard_delegate.h"
#include "input/seat.h"
#include "input/session_gate.h"
#include "scene/node.h"
#include "scene/scene.h"

using namespace cutefish;
using namespace cutefish::input;

namespace {

Timestamp at(int ms)
{
    return Timestamp(std::chrono::milliseconds(ms));
}

bool isModifier(uint32_t evdevCode)
{
    switch (evdevCode) {
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA:
        return true;
    default:
        return false;
    }
}

class RecordingKeyboard : public KeyboardDelegate
{
public:
    explicit RecordingKeyboard(std::vector<uint32_t> *keys)
        : m_keys(keys)
    {
    }

    bool canAcceptKeyboardEventsForSurface(compositor::Surface *) const override { return true; }

    void onKeyboardEnter(compositor::Surface *, const PressedKeys &) override { }

    void onKeyboardLeave(compositor::Surface *) override { }

    uint32_t onKeyboardKey(Timestamp, uint32_t evdevCode, bool pressed) override
    {
        if (!isModifier(evdevCode))
            m_keys->push_back(pressed ? evdevCode : evdevCode | 0x10000);
        return 1;
    }

    void onKeyboardModifiers(const KeyboardModifiers &) override { }

    void onKeyRepeatSettingsChanged(bool, std::chrono::milliseconds,
                                    std::chrono::milliseconds) override
    {
    }

    void onKeyboardLayoutUpdated(const std::string &) override { }

private:
    std::vector<uint32_t> *m_keys;
};

class LockedGate : public SessionGate
{
public:
    bool restrictsInput() const override { return true; }

    scene::Node *inputRoot() const override { return nullptr; }

    bool allowsFocus(const compositor::Surface *) const override { return false; }

    bool allowsActivation() const override { return false; }
};

class VtSwitchTest : public testing::Test
{
protected:
    scene::Scene scene;
    scene::Node *surfaces = scene.root()->addChild(std::make_unique<scene::Node>("surfaces"));
    compositor::Compositor compositor {surfaces};
    Seat seat {&compositor};
    InputManager input {&seat};
    std::unique_ptr<compositor::Surface> surface = compositor.createSurface();
    std::vector<uint32_t> clientKeys;
    Keyboard keyboard {std::make_unique<RecordingKeyboard>(&clientKeys), &seat};
    std::vector<int> switches;
    int time = 0;

    void SetUp() override
    {
        scene.setSize(base::Size(800, 600));
        seat.setFocusedSurface(surface.get());
        input.setVtSwitchHandler([this](int vt) { switches.push_back(vt); });
    }

    void chord(const std::vector<uint32_t> &modifiers, uint32_t key,
               KeyboardModifierLayout layout = KeyboardModifierLayout::Pc)
    {
        for (uint32_t modifier : modifiers)
            input.onKey(at(++time), modifier, true, layout);
        input.onKey(at(++time), key, true, layout);
        input.onKey(at(++time), key, false, layout);
        for (uint32_t modifier : modifiers)
            input.onKey(at(++time), modifier, false, layout);
    }
};

} // namespace

TEST_F(VtSwitchTest, ControlAltFunctionKeysSwitchWithoutReachingTheClient)
{
    chord({KEY_LEFTCTRL, KEY_LEFTALT}, KEY_F2);
    chord({KEY_RIGHTCTRL, KEY_RIGHTALT}, KEY_F10);
    chord({KEY_LEFTCTRL, KEY_LEFTALT}, KEY_F11);
    chord({KEY_LEFTCTRL, KEY_LEFTALT}, KEY_F12);
    EXPECT_EQ(std::vector<int>({2, 10, 11, 12}), switches);
    EXPECT_TRUE(clientKeys.empty());
    EXPECT_TRUE(seat.pressedKeys().empty());
}

TEST_F(VtSwitchTest, OtherChordsReachTheClient)
{
    chord({KEY_LEFTCTRL}, KEY_F2);
    chord({KEY_LEFTALT}, KEY_F2);
    chord({KEY_LEFTCTRL, KEY_LEFTMETA}, KEY_F2);
    chord({KEY_LEFTCTRL, KEY_LEFTALT}, KEY_A);
    EXPECT_TRUE(switches.empty());
    EXPECT_EQ(4u * 2, clientKeys.size());
}

TEST_F(VtSwitchTest, ThePhysicalAltSwitchesUnderTheCommandConvention)
{
    seat.setShortcutConvention(ShortcutConvention::Command);
    chord({KEY_LEFTCTRL, KEY_LEFTALT}, KEY_F3);
    chord({KEY_LEFTCTRL, KEY_LEFTMETA}, KEY_F4);
    chord({KEY_LEFTCTRL, KEY_LEFTALT}, KEY_F5, KeyboardModifierLayout::Apple);
    EXPECT_EQ(std::vector<int>({3, 5}), switches);
}

TEST_F(VtSwitchTest, ALockedSessionStillSwitches)
{
    LockedGate gate;
    seat.setSessionGate(&gate);
    chord({KEY_LEFTCTRL, KEY_LEFTALT}, KEY_F1);
    EXPECT_EQ(std::vector<int>({1}), switches);
    seat.setSessionGate(nullptr);
}

TEST_F(VtSwitchTest, TheReleaseAfterTheSwitchIsSwallowedOnce)
{
    input.onKey(at(++time), KEY_LEFTCTRL, true);
    input.onKey(at(++time), KEY_LEFTALT, true);
    input.onKey(at(++time), KEY_F2, true);
    // The seat is disabled meanwhile, so the modifiers come back released first.
    input.onKey(at(++time), KEY_LEFTALT, false);
    input.onKey(at(++time), KEY_LEFTCTRL, false);
    input.onKey(at(++time), KEY_F2, false);
    EXPECT_TRUE(clientKeys.empty());

    chord({}, KEY_F2);
    EXPECT_EQ(std::vector<uint32_t>({KEY_F2, KEY_F2 | 0x10000}), clientKeys);
    EXPECT_EQ(std::vector<int>({2}), switches);
}
