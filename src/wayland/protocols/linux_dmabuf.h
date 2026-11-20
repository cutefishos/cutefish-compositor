// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/wayland/zwp_linux_dmabuf.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WAYLAND_PROTOCOLS_LINUX_DMABUF_H_
#define CUTEFISH_WAYLAND_PROTOCOLS_LINUX_DMABUF_H_

#include <wayland-server-core.h>

#include <cstdint>
#include <memory>

namespace cutefish::compositor {
class Compositor;
}

namespace cutefish::render {
class DmaBufDevice;
}

namespace cutefish::wayland {

class DmaBufFeedbackManager;

// zwp_linux_dmabuf_v1. Version 4 adds per-surface feedback; the global clamps
// itself to what the platform can actually honour.
constexpr uint32_t kLinuxDmabufVersion = 4;

class LinuxDmabufGlobal
{
public:
    // Returns nullptr when `device` advertises no importable format, in which
    // case the global must not be created at all.
    static std::unique_ptr<LinuxDmabufGlobal> create(wl_display* display,
                                                     compositor::Compositor* compositor,
                                                     render::DmaBufDevice* device);

    LinuxDmabufGlobal(const LinuxDmabufGlobal&) = delete;
    LinuxDmabufGlobal& operator=(const LinuxDmabufGlobal&) = delete;
    ~LinuxDmabufGlobal();

    wl_global* global() const { return m_global; }

    DmaBufFeedbackManager* feedbackManager() const { return m_feedbackManager.get(); }

private:
    LinuxDmabufGlobal(wl_global* global, std::unique_ptr<DmaBufFeedbackManager> manager);

    wl_global* m_global = nullptr;
    std::unique_ptr<DmaBufFeedbackManager> m_feedbackManager;
};

} // namespace cutefish::wayland

#endif // CUTEFISH_WAYLAND_PROTOCOLS_LINUX_DMABUF_H_
