// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RIPPLE_CLIENT_H_
#define CUTEFISH_RIPPLE_CLIENT_H_

#include <cstdint>
#include <functional>
#include <string>

#include "ripple/protocol.h"

namespace cutefish::ripple {

// Talks to other LocalSend devices: the HTTPS requests, with the peer's
// certificate checked against the fingerprint it announced.
class Client
{
public:
    explicit Client(std::string fingerprint);
    ~Client();

    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // POST a JSON body; the reply is left in reply. 0 when the request never
    // got through, with the reason in error.
    int postJson(const Device &peer, const std::string &path, const std::string &body,
                 std::string &reply, std::string &error);

    // POST a file's bytes; progress gets the bytes sent so far. 0 when the
    // request failed.
    int postFile(const Device &peer, const std::string &path, const std::string &file,
                 const std::function<void(uint64_t)> &progress, std::string &error);

private:
    std::string m_fingerprint;
};

// Our own facts, in the shape every request carries.
std::string deviceJson(const Device &device);

} // namespace cutefish::ripple

#endif // CUTEFISH_RIPPLE_CLIENT_H_
