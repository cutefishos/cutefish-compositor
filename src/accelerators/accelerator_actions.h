// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/public/cpp/accelerator_actions.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_ACTIONS_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_ACTIONS_H_

#include <cstdint>
#include <optional>
#include <vector>

#include <string>
#include <string_view>

namespace cutefish::accelerators {

// Persisted and exposed over D-Bus by name, never by value, so the order here
// is free to change.
enum class AcceleratorAction : uint32_t {
    ToggleAppList,
    LockScreen,

    CycleForwardMru,
    CycleBackwardMru,
    ToggleMaximized,
    WindowMinimize,
    QuitApplication,
    HideApplication,
    HideOtherApplications,
    ToggleFullscreen,
    WindowCycleSnapLeft,
    WindowCycleSnapRight,
    CloseWindow,
    MoveActiveWindowBetweenDisplays,
    ToggleOverview,
    ToggleShowDesktop,
    DesksActivateDeskLeft,
    DesksActivateDeskRight,
    DesksMoveActiveItemLeft,
    DesksMoveActiveItemRight,
    DesksNewDesk,
    DesksRemoveCurrentDesk,
    DesksActivate0,
    DesksActivate1,
    DesksActivate2,
    DesksActivate3,
    DesksActivate4,
    DesksActivate5,
    DesksActivate6,
    DesksActivate7,
    DesksToggleAssignToAllDesks,

    TakeScreenshot,
    TakeScreenshotToClipboard,
    TakePartialScreenshot,
    TakePartialScreenshotToClipboard,
    StartCaptureMode,
    StopScreenRecording,

    ToggleClipboardHistory,

    MediaPlayPause,
    MediaPause,
    MediaPrevious,
    MediaNext,
    MediaStop,

    VolumeMuteToggle,
    VolumeDown,
    VolumeUp,
    MicrophoneMuteToggle,
    BrightnessDown,
    BrightnessUp,
    KeyboardBrightnessDown,
    KeyboardBrightnessUp,
};

using AcceleratorActionId = uint32_t;

constexpr AcceleratorActionId toActionId(AcceleratorAction action)
{
    return static_cast<AcceleratorActionId>(action);
}

// The kebab-case identifier used in preferences and on the bus.
std::string acceleratorActionName(AcceleratorAction action);
std::optional<AcceleratorAction> acceleratorActionFromName(std::string_view name);

// An action need not have a default binding to exist.
std::vector<AcceleratorAction> allAcceleratorActions();

} // namespace cutefish::accelerators

#endif // CUTEFISH_ACCELERATORS_ACCELERATOR_ACTIONS_H_
