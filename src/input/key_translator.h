// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_INPUT_KEY_TRANSLATOR_H_
#define CUTEFISH_INPUT_KEY_TRANSLATOR_H_

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include <string_view>

#include "input/types.h"

namespace cutefish::input {

class XkbTracker;

bool isTerminal(std::string_view applicationId);

// A shortcut as the user presses it: `command` is the Command key under the
// Command convention, `control` the Control key.
struct PressedShortcut {
    bool command = false;
    bool control = false;
    bool alt = false;
    bool shift = false;
};

// What the user presses for a client to get `keysym` with these modifiers: KeyTranslator's rules run backwards.
PressedShortcut pressedShortcut(ShortcutConvention convention, bool terminal, uint32_t keysym, bool ctrl, bool alt,
                                bool shift);

// Each key remembers what it was sent as, so its release matches whatever changed in between.
class KeyTranslator
{
public:
    using DeliverKey = std::function<void(Timestamp, uint32_t evdevCode, bool pressed)>;

    // `keymap` is the tracker the delivered keys feed.
    KeyTranslator(const XkbTracker *keymap, DeliverKey deliver);

    ShortcutConvention convention() const { return m_convention; }
    void setConvention(ShortcutConvention convention) { m_convention = convention; }

    void setTerminalProvider(std::function<bool()> provider)
    {
        m_terminal = std::move(provider);
    }
    bool terminalFocused() const { return m_terminal && m_terminal(); }

    void setCommandBindingProvider(std::function<bool()> provider)
    {
        m_bindsCommand = std::move(provider);
    }

    void setPassthroughProvider(std::function<bool()> provider)
    {
        m_passthrough = std::move(provider);
    }

    // A drag reads the modifiers as they change, so none are held back while a button is held.
    void setPointerButtonsProvider(std::function<bool()> provider)
    {
        m_pointerButtonsHeld = std::move(provider);
    }

    void onKey(Timestamp timestamp, uint32_t evdevCode, bool pressed);

    void flushModifiers(Timestamp timestamp);

    uint32_t translateButton(uint32_t evdevButton, bool pressed);

    std::vector<uint32_t> deliveredKeys() const;

private:
    struct Press {
        uint32_t delivered = 0; // 0 when nothing stays pressed
        bool rule = false;
    };

    bool passthrough() const;
    bool isModifier(uint32_t code) const;
    bool isHeldBack(uint32_t code) const;
    bool deliveredAtOnce(uint32_t code) const;
    uint32_t deliveredModifier(uint32_t code, bool passthrough) const;
    uint32_t heldRoles() const;
    std::set<uint32_t> wantedModifiers(bool passthrough, bool immediateOnly) const;

    void syncModifiers(Timestamp timestamp, const std::set<uint32_t> &target);
    void syncModifierKinds(Timestamp timestamp, uint32_t kinds);
    void pressKey(Timestamp timestamp, uint32_t code);
    void releaseKey(Timestamp timestamp, uint32_t code);
    bool applyRule(Timestamp timestamp, uint32_t code);

    const XkbTracker *m_keymap;
    DeliverKey m_deliver;
    ShortcutConvention m_convention = ShortcutConvention::Pc;
    std::function<bool()> m_terminal;
    std::function<bool()> m_passthrough;
    std::function<bool()> m_bindsCommand;
    std::function<bool()> m_pointerButtonsHeld;

    // In the order they went down.
    std::vector<uint32_t> m_heldModifiers;
    std::set<uint32_t> m_deliveredModifiers;
    std::map<uint32_t, Press> m_presses;
    // Counted: two physical keys can map to the same one.
    std::map<uint32_t, int> m_deliveredKeys;
    bool m_secondaryClick = false;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_KEY_TRANSLATOR_H_
