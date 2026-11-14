// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_BACKEND_SESSION_LIBSEAT_SESSION_H_
#define CUTEFISH_BACKEND_SESSION_LIBSEAT_SESSION_H_


#include <memory>
#include <string>
#include <unordered_map>

#include "backend/session/session.h"

struct libseat;

namespace cutefish::backend {

// Session backed by libseat (seatd or logind).
class LibseatSession : public Session
{
public:
    ~LibseatSession() override;

    // Opens the seat and pumps libseat until the initial enable event arrives.
    // Returns nullptr if no seat could be acquired.
    static std::unique_ptr<LibseatSession> create();

    std::string seatName() const override { return m_seatName; }
    bool isActive() const override { return m_active; }

    int openDevice(const std::string& path, int* deviceId) override;
    void closeDevice(int deviceId) override;
    bool switchSession(int vt) override;

private:
    LibseatSession() = default;

    void dispatch();

    static void onEnableSeat(libseat* seat, void* userData);
    static void onDisableSeat(libseat* seat, void* userData);

    libseat* m_seat = nullptr;
    int m_watch = 0;
    std::string m_seatName;
    bool m_active = false;
    // Device id -> the descriptor libseat handed out, which libseat never closes.
    std::unordered_map<int, int> m_deviceFds;
};

} // namespace cutefish::backend

#endif // CUTEFISH_BACKEND_SESSION_LIBSEAT_SESSION_H_
