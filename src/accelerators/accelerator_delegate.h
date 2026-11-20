// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_ACCELERATORS_ACCELERATOR_DELEGATE_H_
#define CUTEFISH_ACCELERATORS_ACCELERATOR_DELEGATE_H_

#include <functional>

namespace cutefish::accelerators {

class AcceleratorDelegate
{
public:
    virtual ~AcceleratorDelegate() = default;

    // org.cutefish.Launchpad.toggle.
    virtual void toggleAppList() = 0;

    // org.cutefish.Screenshot.
    virtual void takeScreenshot(bool toClipboard) = 0;
    virtual void takePartialScreenshot(bool toClipboard) = 0;
    virtual void startCaptureMode() = 0;
    virtual void stopScreenRecording() = 0;

    // org.cutefish.ClipboardHistory.toggle.
    virtual void toggleClipboardHistory() = 0;

    // org.cutefish.MenuBar: control the current MPRIS player.
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

class DBusAcceleratorDelegate : public AcceleratorDelegate
{
public:
    // The launchpad drawn in this process; unset, the call goes over D-Bus.
    std::function<void()> launchpadToggle;

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
