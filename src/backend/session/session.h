// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_BACKEND_SESSION_SESSION_H_
#define CUTEFISH_BACKEND_SESSION_SESSION_H_

#include <string>

#include "base/signal.h"

namespace cutefish::backend {

class Session
{
public:
    Session() = default;
    virtual ~Session() = default;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    virtual std::string seatName() const = 0;

    // No DRM ioctl that requires master may be issued while inactive.
    virtual bool isActive() const = 0;

    // Returns -1 on failure; closing the device closes the descriptor too.
    virtual int openDevice(const std::string& path, int* deviceId) = 0;
    virtual void closeDevice(int deviceId) = 0;

    // Asks the seat to bring another session to the foreground, such as the
    // one on virtual terminal `vt`; this one is disabled once it has.
    virtual void switchSession(int vt) = 0;

    // Emitted before the seat is released, so listeners can stop touching
    // devices first.
    base::Signal<bool> activeChanged;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_SESSION_SESSION_H_
