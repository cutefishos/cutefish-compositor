// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/accelerators/accelerator_commands.h
// Revision: 8abbac30b9cf2fe976422c58c000c94685648b65

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_COMMANDS_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_COMMANDS_H_

#include "accelerators/accelerator_actions.h"

namespace cutefish::session {
class ScreenSaverService;
class SessionController;
} // namespace cutefish::session

namespace cutefish::wm {
class Window;
class WindowCycleController;
class WindowManager;
} // namespace cutefish::wm

namespace cutefish::accelerators {

class AcceleratorDelegate;

struct CommandContext {
    wm::WindowManager *windowManager = nullptr;
    wm::WindowCycleController *windowCycle = nullptr;
    session::SessionController *session = nullptr;
    session::ScreenSaverService *screenSaver = nullptr;
    AcceleratorDelegate *delegate = nullptr;
};

namespace commands {

// The active window, when it is a visible application window.
wm::Window *targetWindow(const CommandContext &context);

bool canCycleMru(const CommandContext &context);
bool canLock(const CommandContext &context);
bool canWindowSnap(const CommandContext &context);
bool canMoveActiveWindowBetweenDisplays(const CommandContext &context);
bool canCloseWindow(const CommandContext &context);
bool canToggleOverview(const CommandContext &context);
bool canToggleShowDesktop(const CommandContext &context);

void cycleForwardMru(const CommandContext &context);
void cycleBackwardMru(const CommandContext &context);
void toggleMaximized(const CommandContext &context);
void windowMinimize(const CommandContext &context);
// Quit waits 100 ms first.
void quitApplication(const CommandContext &context);
void hideApplication(const CommandContext &context);
void hideOtherApplications(const CommandContext &context);
void toggleFullscreen(const CommandContext &context);
void windowSnap(const CommandContext &context, AcceleratorAction action);
void closeWindow(const CommandContext &context);
void moveActiveWindowBetweenDisplays(const CommandContext &context);
void toggleOverview(const CommandContext &context);
void toggleShowDesktop(const CommandContext &context);
void activateDesk(const CommandContext &context, bool activateLeft);
void activateDeskAtIndex(const CommandContext &context, AcceleratorAction action);
void moveActiveItem(const CommandContext &context, bool goingLeft);
void newDesk(const CommandContext &context);
void removeCurrentDesk(const CommandContext &context);
void toggleAssignToAllDesks(const CommandContext &context);
void lockScreen(const CommandContext &context);
void toggleAppList(const CommandContext &context);
// Screen capture belongs to the shell; these only ask it.
void takeScreenshot(const CommandContext &context, bool toClipboard);
void takePartialScreenshot(const CommandContext &context, bool toClipboard);
void startCaptureMode(const CommandContext &context);
void stopScreenRecording(const CommandContext &context);
void toggleClipboardHistory(const CommandContext &context);
void mediaPlayPause(const CommandContext &context);
void mediaPause(const CommandContext &context);
void mediaPrevious(const CommandContext &context);
void mediaNext(const CommandContext &context);
void mediaStop(const CommandContext &context);
void volumeDown(const CommandContext &context);
void volumeUp(const CommandContext &context);
void volumeMuteToggle(const CommandContext &context);
void microphoneMuteToggle(const CommandContext &context);
void brightnessDown(const CommandContext &context);
void brightnessUp(const CommandContext &context);
void keyboardBrightnessDown(const CommandContext &context);
void keyboardBrightnessUp(const CommandContext &context);

} // namespace commands
} // namespace cutefish::accelerators

#endif // CUTEFISH_ACCELERATORS_ACCELERATOR_COMMANDS_H_
