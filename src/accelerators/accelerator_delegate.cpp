// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "accelerators/accelerator_delegate.h"

#include "base/dbus.h"

namespace cutefish::accelerators {

namespace {

// Calls without waiting for the answer.
template<typename... Args>
void call(const char *service, const char *path, const char *method, const Args &...arguments)
{
    if (base::dbus::Connection *bus = base::dbus::Connection::session())
        base::dbus::Proxy(bus, service, path, service).send(method, arguments...);
}

template<typename... Args>
void callScreenshot(const char *method, const Args &...arguments)
{
    call("org.cutefish.Screenshot", "/Screenshot", method, arguments...);
}

void callOsd(const char *method)
{
    call("org.cutefish.Osd", "/Osd", method);
}

} // namespace

void DBusAcceleratorDelegate::toggleAppList()
{
    if (launcherToggle) {
        launcherToggle();
        return;
    }
    call("org.cutefish.Launcher", "/Launcher", "toggle");
}

void DBusAcceleratorDelegate::takeScreenshot(bool toClipboard)
{
    callScreenshot("takeScreenshot", toClipboard);
}

void DBusAcceleratorDelegate::takePartialScreenshot(bool toClipboard)
{
    callScreenshot("takePartialScreenshot", toClipboard);
}

void DBusAcceleratorDelegate::startCaptureMode()
{
    callScreenshot("startCaptureMode");
}

void DBusAcceleratorDelegate::stopScreenRecording()
{
    callScreenshot("stopRecording");
}

void DBusAcceleratorDelegate::toggleClipboardHistory()
{
    call("org.cutefish.ClipboardHistory", "/ClipboardHistory", "toggle");
}

void DBusAcceleratorDelegate::mediaPlayPause()
{
    call("org.cutefish.Statusbar", "/Statusbar", "mediaPlayPause");
}

void DBusAcceleratorDelegate::mediaPause()
{
    call("org.cutefish.Statusbar", "/Statusbar", "mediaPause");
}

void DBusAcceleratorDelegate::mediaPrevious()
{
    call("org.cutefish.Statusbar", "/Statusbar", "mediaPrevious");
}

void DBusAcceleratorDelegate::mediaNext()
{
    call("org.cutefish.Statusbar", "/Statusbar", "mediaNext");
}

void DBusAcceleratorDelegate::mediaStop()
{
    call("org.cutefish.Statusbar", "/Statusbar", "mediaStop");
}

void DBusAcceleratorDelegate::volumeDown()
{
    callOsd("volumeDown");
}

void DBusAcceleratorDelegate::volumeUp()
{
    callOsd("volumeUp");
}

void DBusAcceleratorDelegate::toggleMute()
{
    callOsd("toggleMute");
}

void DBusAcceleratorDelegate::toggleMicrophoneMute()
{
    callOsd("toggleMicrophoneMute");
}

void DBusAcceleratorDelegate::brightnessDown()
{
    callOsd("brightnessDown");
}

void DBusAcceleratorDelegate::brightnessUp()
{
    callOsd("brightnessUp");
}

void DBusAcceleratorDelegate::keyboardBrightnessDown()
{
    callOsd("keyboardBrightnessDown");
}

void DBusAcceleratorDelegate::keyboardBrightnessUp()
{
    callOsd("keyboardBrightnessUp");
}

} // namespace cutefish::accelerators
