// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_BACKEND_DRM_DRM_SELECTION_H_
#define CUTEFISH_BACKEND_DRM_DRM_SELECTION_H_

#include <xf86drmMode.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cutefish::backend {

struct ConnectorInfo {
    uint32_t id = 0;
    uint32_t type = 0;
    uint32_t typeId = 0;
    uint32_t connection = 0;
    int modeCount = 0;
    // A head-mounted display, which a VR runtime leases instead.
    bool nonDesktop = false;
};

bool isDesktopOutput(const ConnectorInfo& connector);

std::optional<int> selectConnector(const std::vector<ConnectorInfo>& connectors);

// Preferred mode, else the largest progressive one by pixel area (highest
// refresh breaks ties), else the largest interlaced one, else nothing.
std::optional<drmModeModeInfo> selectMode(const drmModeModeInfo* modes, int count);

// Modes differing only in sync positions count as one; a preferred duplicate
// replaces the one kept.
std::vector<drmModeModeInfo> distinctModes(const drmModeModeInfo* modes, int count);

// `crtcIds` is in drmModeRes order; the connector's current CRTC wins when
// free, avoiding a modeset. 0 when none is available.
uint32_t selectCrtc(uint32_t possibleCrtcs,
                    const std::vector<uint32_t>& crtcIds,
                    uint32_t usedCrtcMask,
                    uint32_t currentCrtcId = 0);

struct DrmDeviceCandidate {
    std::string path;
    std::string driver;
    // /sys/.../device/boot_vga.
    bool bootVga = false;
};

// The boot display first, then the preferred drivers, then enumeration order.
std::vector<std::string> orderDisplayDevices(const std::vector<DrmDeviceCandidate>& candidates);

double refreshRate(const drmModeModeInfo& mode);

std::string connectorName(uint32_t type, uint32_t typeId);

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_DRM_DRM_SELECTION_H_
