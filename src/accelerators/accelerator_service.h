// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/webui/shortcut_customization_ui/backend/
//           accelerator_configuration_provider.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_SERVICE_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_SERVICE_H_

#include <memory>
#include <optional>
#include <string>

#include "accelerators/accelerator.h"
#include "accelerators/accelerator_actions.h"
#include "accelerators/accelerator_configuration.h"
#include "base/dbus.h"
#include "base/json.h"
#include "base/signal.h"

namespace cutefish::input {
class DBusExport;
class XkbTracker;
}

namespace cutefish::accelerators {

class AcceleratorController;

class AcceleratorService
{
public:
    AcceleratorService(AcceleratorConfiguration *configuration, AcceleratorController *controller,
                       const input::XkbTracker *xkb);
    ~AcceleratorService();

    bool registerService();

    // "pc" or "command"; see input::ShortcutConvention.
    std::string shortcutConvention() const;

    base::json::Value acceleratorToJson(const Accelerator &accelerator,
                                        AcceleratorActionId id) const;

    // {"actions": [{"name", "accelerators": [...], "defaults": [...]}]}.
    std::string accelerators();

    // Every mutation answers {"result": <AcceleratorConfigResult name>,
    // "conflictAction": <action name, when the result names one>}. `keycode` is the
    // evdev code, 0 when unknown. A Meta keycode with no modifiers binds the Meta tap.
    std::string addAccelerator(const std::string &action, uint32_t keycode, uint32_t keysym,
                               uint32_t modifiers);
    std::string removeAccelerator(const std::string &action, uint32_t keysym, uint32_t modifiers);
    std::string replaceAccelerator(const std::string &action, uint32_t oldKeysym,
                                   uint32_t oldModifiers, uint32_t keycode, uint32_t keysym,
                                   uint32_t modifiers);
    std::string restoreDefault(const std::string &action);
    std::string restoreAllDefaults();

    // {"keysym", "keyName", "keyText", "modifierKey"} for a key the recorder saw.
    std::string describeKey(uint32_t keycode, uint32_t keysym);

    // Also resets a pending confirmation. Over the bus the caller owns the flag; it is
    // lifted when the caller leaves the bus.
    void preventProcessingAccelerators(bool prevent);

    bool hasCustomAccelerators();

    base::Signal<> acceleratorsChanged;

private:
    enum class ConflictErrorState {
        Standby,
        AwaitingConflictResolution,
        AwaitingNonMetaConfirmation,
    };

    struct PendingAccelerator {
        Accelerator accelerator;
        AcceleratorActionId action;
    };

    void preventProcessingAccelerators(bool prevent, const std::string &owner);
    Accelerator resolve(uint32_t keycode, uint32_t keysym, uint32_t modifiers) const;
    std::optional<AcceleratorConfigResult> validateAction(const std::string &name,
                                                          AcceleratorActionId *id) const;
    std::optional<AcceleratorConfigResult> validateAccelerator(const Accelerator &accelerator) const;
    std::optional<base::json::Value> preprocessAddAccelerator(AcceleratorActionId id,
                                                              const Accelerator &accelerator);
    ConflictErrorState maybeHandleNonMetaAccelerator(const Accelerator &accelerator,
                                                     AcceleratorActionId id);
    std::string result(AcceleratorConfigResult result, const std::string &conflictAction = {}) const;
    void setPreventOwner(const std::string &owner);
    void releasePreventOwner(const std::string &owner);

    AcceleratorConfiguration *m_configuration = nullptr;
    AcceleratorController *m_controller = nullptr;
    const input::XkbTracker *m_xkb = nullptr;
    std::unique_ptr<input::DBusExport> m_object;
    base::Subscription m_configurationUpdates;

    std::string m_preventOwner;
    base::dbus::NameWatch m_preventOwnerWatch;

    std::optional<PendingAccelerator> m_pending;
    ConflictErrorState m_conflictState = ConflictErrorState::Standby;
};

} // namespace cutefish::accelerators

#endif // CUTEFISH_ACCELERATORS_ACCELERATOR_SERVICE_H_
