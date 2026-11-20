// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_SERVICES_H_
#define CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_SERVICES_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "base/signal.h"

namespace cutefish::shell {

struct WirelessNetwork {
    enum class State {
        Activating,
        Activated,
        Deactivating,
        Deactivated,
    };
    enum class Security {
        None,
        Unknown,
        Password,
        Enterprise,
        Other,
    };

    std::string key;
    std::string name;
    std::string icon;
    State state = State::Deactivated;
    Security security = Security::None;
    std::string connectionPath;
    std::string devicePath;
    std::string specificPath;

    bool operator==(const WirelessNetwork &) const = default;
};

struct BluetoothDevice {
    std::string path;
    std::string name;
    // Empty when BlueZ gives none.
    std::string icon;
    bool connected = false;
    bool busy = false;
    // -1 when the device does not report one.
    int batteryPercentage = -1;

    bool operator==(const BluetoothDevice &) const = default;
};

struct AudioOutput {
    uint32_t id = 0;
    std::string name;
    bool isDefault = false;

    bool operator==(const AudioOutput &) const = default;
};

struct MediaState {
    // False when stopped; a paused player counts.
    bool hasPlayer = false;
    bool playing = false;
    std::string title;
    std::string identity;
    std::string artist;
    std::string artUrl;
    bool canControl = false;
    bool canGoPrevious = false;
    bool canGoNext = false;
    bool canPlay = false;
    bool canPause = false;

    bool operator==(const MediaState &) const = default;
};

struct DeviceUser {
    std::string name;
    std::string icon;
    bool camera = false;
    bool microphone = false;
    bool systemAudio = false;

    bool operator==(const DeviceUser &) const = default;
};

// Setters return at once; the change comes back through `changed`.
class ControlCenterServices
{
public:
    virtual ~ControlCenterServices() = default;

    virtual bool wirelessHardwareEnabled() const = 0;
    virtual bool wirelessEnabled() const = 0;
    virtual bool hotspotActive() const = 0;
    virtual std::string hotspotName() const = 0;
    virtual std::string wirelessName() const = 0;
    virtual std::string wirelessIcon() const = 0;
    virtual std::vector<WirelessNetwork> wirelessNetworks() const = 0;
    virtual void enableWireless(bool enable) = 0;
    virtual void requestScan() = 0;
    virtual void stopHotspot() = 0;
    // True when the panel closes for a window that asks for more.
    virtual bool activateNetwork(const WirelessNetwork &network) = 0;

    virtual bool bluetoothAvailable() const = 0;
    virtual bool bluetoothEnabled() const = 0;
    virtual void setBluetoothEnabled(bool enabled) = 0;
    virtual std::vector<BluetoothDevice> bluetoothDevices() const = 0;
    virtual void setBluetoothDeviceConnected(const std::string &path, bool connected) = 0;

    virtual void setDarkMode(bool dark) = 0;
    virtual bool doNotDisturb() const = 0;
    virtual void setDoNotDisturb(bool enabled) = 0;
    virtual bool cameraEnabled() const = 0;
    virtual void setCameraEnabled(bool enabled) = 0;
    virtual bool microphoneEnabled() const = 0;
    virtual void setMicrophoneEnabled(bool enabled) = 0;
    virtual std::vector<DeviceUser> deviceUsers() const = 0;

    virtual MediaState media() const = 0;
    virtual void mediaPrevious() = 0;
    virtual void mediaPlay() = 0;
    virtual void mediaPause() = 0;
    virtual void mediaNext() = 0;

    // Brightness in percent.
    virtual bool brightnessEnabled() const = 0;
    virtual int brightness() const = 0;
    virtual void setBrightness(int value) = 0;

    // Volume on PipeWire's cubic scale, 1 being normal.
    virtual bool hasSink() const = 0;
    virtual double volume() const = 0;
    virtual bool muted() const = 0;
    virtual void setVolume(double volume) = 0;
    virtual void setMuted(bool muted) = 0;
    virtual std::vector<AudioOutput> outputs() const = 0;
    virtual void setDefaultOutput(uint32_t id) = 0;

    virtual int chargePercent() const = 0;
    virtual bool onBattery() const = 0;
    virtual std::string batteryStatus() const = 0;
    virtual std::vector<std::string> powerProfiles() const = 0;
    virtual std::string powerProfile() const = 0;
    virtual void setPowerProfile(const std::string &profile) = 0;

    virtual void openSettings(const std::vector<std::string> &arguments) = 0;

    base::Signal<> changed;
    base::Signal<const std::string &, const std::string &> vpnFailed;
    std::function<std::vector<DeviceUser>()> deviceUsersSource;
};

std::string volumeIconName(bool hasSink, double volume);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_CONTROL_CENTER_SERVICES_H_
