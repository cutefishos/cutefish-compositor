// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "accelerators/menu_shortcut.h"

#include <cctype>

#include <xkbcommon/xkbcommon.h>

#include "accelerators/accelerator.h"
#include "accelerators/accelerator_configuration.h"
#include "input/key_translator.h"
#include "ui/event.h"

namespace cutefish::accelerators {

std::string menuShortcutText(const std::string &key, uint32_t modifiers, input::ShortcutConvention convention,
                             bool terminal, bool bindsCommand, const AcceleratorConfiguration *configuration)
{
    uint32_t keysym = XKB_KEY_NoSymbol;
    if (key.size() == 1)
        keysym = xkb_utf32_to_keysym(uint32_t(std::tolower(static_cast<unsigned char>(key[0]))));
    if (keysym == XKB_KEY_NoSymbol)
        keysym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_NO_FLAGS);
    if (keysym == XKB_KEY_NoSymbol)
        return {};

    const bool ctrl = modifiers & ui::ControlModifier;
    const bool alt = modifiers & ui::AltModifier;
    const bool shift = modifiers & ui::ShiftModifier;
    input::PressedShortcut pressed = input::pressedShortcut(convention, terminal, keysym, ctrl, alt, shift);
    // A Command shortcut the compositor takes never reaches the application,
    // so the Control key is the one to press; Quit is the same either way.
    // A program that binds Command gets Control as its own Control key, so
    // nothing else reaches the entry, and the compositor's Command shortcuts
    // there are the ones its platform reserves for the same actions.
    if (pressed.command && !bindsCommand && configuration) {
        uint32_t flags = MetaDown;
        if (pressed.alt)
            flags |= AltDown;
        if (pressed.shift)
            flags |= ShiftDown;
        const AcceleratorAction *action = configuration->findAcceleratorAction(Accelerator(keysym, flags));
        if (action && *action != AcceleratorAction::QuitApplication)
            pressed = {false, ctrl, alt, shift};
    }

    std::string text;
    if (pressed.control)
        text += "Ctrl+";
    if (pressed.command)
        text += "Command+";
    if (pressed.alt)
        text += "Alt+";
    if (pressed.shift)
        text += "Shift+";
    if (modifiers & ui::SuperModifier)
        text += "Super+";
    if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z')
        text += char(key[0] - 'a' + 'A');
    else
        text += key;
    return text;
}

} // namespace cutefish::accelerators
