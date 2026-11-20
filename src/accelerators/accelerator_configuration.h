// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/accelerators/ash_accelerator_configuration.h and
//           chromium/src/ash/public/cpp/accelerator_configuration.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_CONFIGURATION_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_CONFIGURATION_H_

#include <map>
#include <optional>
#include <span>
#include <vector>

#include "accelerators/accelerator.h"
#include "accelerators/accelerator_actions.h"
#include "accelerators/accelerator_map.h"
#include "accelerators/accelerator_table.h"
#include "base/json.h"
#include "base/settings.h"
#include "base/signal.h"
#include "input/types.h"

namespace cutefish::accelerators {

enum class AcceleratorConfigResult {
    Success,
    Conflict,
    ConflictCanOverride,
    NotFound,
    KeyNotAllowed,
    MissingModifier,
    ShiftOnlyNotAllowed,
    MaximumAcceleratorsReached,
    RestoreSuccessWithConflicts,
    NonMetaAcceleratorWarning,
    ReservedKeyNotAllowed,
};

const char *toString(AcceleratorConfigResult result);

using ActionIdToAcceleratorsMap = std::map<AcceleratorActionId, std::vector<Accelerator>>;

class AcceleratorConfiguration
{
public:
    // `settingsFile` overrides the compositor's settings file.
    explicit AcceleratorConfiguration(const std::string &settingsFile = {});
    ~AcceleratorConfiguration();

    void initialize();
    void initialize(std::span<const AcceleratorData> accelerators);

    input::ShortcutConvention convention() const { return m_convention; }

    // Each convention has its own defaults and saved overrides; a change reloads both.
    void setConvention(input::ShortcutConvention convention);

    const std::vector<Accelerator> &allAccelerators() const { return m_accelerators; }

    const ActionIdToAcceleratorsMap &acceleratorMapping() const { return m_idToAccelerators; }

    std::optional<std::vector<Accelerator>> acceleratorsForAction(AcceleratorActionId id) const;

    const AcceleratorAction *findAcceleratorAction(const Accelerator &accelerator) const;

    std::optional<AcceleratorAction> idForDefaultAccelerator(const Accelerator &accelerator) const;
    std::vector<Accelerator> defaultAcceleratorsForId(AcceleratorActionId id) const;

    bool isValid(AcceleratorActionId id) const;
    bool hasCustomAccelerators() const;

    AcceleratorConfigResult addUserAccelerator(AcceleratorActionId id,
                                               const Accelerator &accelerator);
    // Deletes a user accelerator or disables a default one.
    AcceleratorConfigResult removeAccelerator(AcceleratorActionId id,
                                              const Accelerator &accelerator);
    // Atomic remove-then-add.
    AcceleratorConfigResult replaceAccelerator(AcceleratorActionId id,
                                               const Accelerator &oldAccelerator,
                                               const Accelerator &newAccelerator);
    AcceleratorConfigResult restoreDefault(AcceleratorActionId id);
    AcceleratorConfigResult restoreAllDefaults();

    void setUsePositionalLookup(bool use);
    bool usesPositionalLookup() const { return m_acceleratorToId.usePositionalLookup(); }

    enum class ModificationAction {
        Add,
        Remove,
    };

    base::Signal<> acceleratorsUpdated;

private:
    using AcceleratorActionMap = AcceleratorMap<AcceleratorAction>;

    void addAccelerators(std::span<const AcceleratorData> accelerators);

    std::string overridesGroup() const;
    void loadOverrides();
    void saveOverrides();
    void applyOverrides();
    void updateOverrides(AcceleratorActionId id, const Accelerator &accelerator,
                         ModificationAction action);
    bool hasOverride(AcceleratorActionId id, const Accelerator &accelerator,
                     ModificationAction action) const;
    void restoreDefaultOwner(const Accelerator &accelerator);

    AcceleratorConfigResult doRemoveAccelerator(AcceleratorActionId id,
                                                const Accelerator &accelerator,
                                                bool saveOverride);
    AcceleratorConfigResult doAddAccelerator(AcceleratorActionId id,
                                             const Accelerator &accelerator, bool saveOverride);
    AcceleratorConfigResult doReplaceAccelerator(AcceleratorActionId id,
                                                 const Accelerator &oldAccelerator,
                                                 const Accelerator &newAccelerator);

    void updateAndNotifyAccelerators();
    bool areAcceleratorsValid();
    void resetAllAccelerators();
    int totalNumberOfModifications() const;

    base::Settings m_settings;
    input::ShortcutConvention m_convention = input::ShortcutConvention::Pc;

    // Action name -> list of {key, modifiers, state, action} objects.
    std::map<std::string, base::json::Value::Array> m_overrides;

    std::vector<Accelerator> m_accelerators;

    ActionIdToAcceleratorsMap m_idToAccelerators;
    AcceleratorActionMap m_acceleratorToId;

    // Never modified after initialize().
    ActionIdToAcceleratorsMap m_defaultIdToAccelerators;
    AcceleratorActionMap m_defaultAcceleratorsToId;
};

} // namespace cutefish::accelerators

#endif // CUTEFISH_ACCELERATORS_ACCELERATOR_CONFIGURATION_H_
