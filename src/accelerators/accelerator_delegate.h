// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// What the accelerator commands need from outside the compositor process.
// Ash performs every action in-process through Shell::Get(); Cutefish's shell
// UI is a separate process reached over D-Bus, and this is the seam the tests
// replace.

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_DELEGATE_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_DELEGATE_H_

#include <functional>

namespace cutefish::accelerators {

class AcceleratorDelegate
{
public:
    virtual ~AcceleratorDelegate() = default;

    // org.cutefish.Launcher.toggle.
    virtual void toggleAppList() = 0;

    // org.cutefish.Screenshot: every screen at once, a part of one chosen
    // with the pointer, the capture tools, and the end of a recording.
    virtual void takeScreenshot(bool toClipboard) = 0;
    virtual void takePartialScreenshot(bool toClipboard) = 0;
    virtual void startCaptureMode() = 0;
    virtual void stopScreenRecording() = 0;

    // org.cutefish.ClipboardHistory.toggle: shows or hides the picker.
    virtual void toggleClipboardHistory() = 0;

    // org.cutefish.Statusbar: control the current MPRIS player.
    virtual void mediaPlayPause() = 0;
    virtual void mediaPause() = 0;
    virtual void mediaPrevious() = 0;
    virtual void mediaNext() = 0;
    virtual void mediaStop() = 0;

    // org.cutefish.Osd: changes the level and shows it.
    virtual void volumeDown() = 0;
    virtual void volumeUp() = 0;
    virtual void toggleMute() = 0;
    virtual void toggleMicrophoneMute() = 0;
    virtual void brightnessDown() = 0;
    virtual void brightnessUp() = 0;
    virtual void keyboardBrightnessDown() = 0;
    virtual void keyboardBrightnessUp() = 0;
};

// The production delegate.
class DBusAcceleratorDelegate : public AcceleratorDelegate
{
public:
    // The launcher drawn in this process; unset, the call goes over D-Bus.
    std::function<void()> launcherToggle;

    void toggleAppList() override;
    void takeScreenshot(bool toClipboard) override;
    void takePartialScreenshot(bool toClipboard) override;
    void startCaptureMode() override;
    void stopScreenRecording() override;
    void toggleClipboardHistory() override;
    void mediaPlayPause() override;
    void mediaPause() override;
    void mediaPrevious() override;
    void mediaNext() override;
    void mediaStop() override;
    void volumeDown() override;
    void volumeUp() override;
    void toggleMute() override;
    void toggleMicrophoneMute() override;
    void brightnessDown() override;
    void brightnessUp() override;
    void keyboardBrightnessDown() override;
    void keyboardBrightnessUp() override;
};

} // namespace cutefish::accelerators

#endif // CUTEFISH_ACCELERATORS_ACCELERATOR_DELEGATE_H_
