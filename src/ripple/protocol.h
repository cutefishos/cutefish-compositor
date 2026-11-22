// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RIPPLE_PROTOCOL_H_
#define CUTEFISH_RIPPLE_PROTOCOL_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/json.h"

namespace cutefish::ripple {

// LocalSend protocol v2.2. One value for the device's identity, on the wire in
// announcements, /register bodies and the info in a prepare-upload.
struct Device
{
    std::string alias;
    // "mobile", "desktop", "web", "headless" or "server"; empty is unknown.
    std::string type = "desktop";
    std::string model;
    // "http" or "https".
    std::string protocol = "https";
    // SHA-256 of the certificate, uppercase hex, when HTTPS; random otherwise.
    std::string fingerprint;
    // Where the peer answers, filled from the packet rather than the JSON.
    std::string host;
    int port = 53317;
    bool download = false;

    base::json::Value toJson(bool announce = false) const;
    static std::optional<Device> fromJson(const base::json::Value &value);

    bool operator==(const Device &other) const { return fingerprint == other.fingerprint; }
};

struct FileInfo
{
    std::string id;
    std::string name;
    std::string type;
    uint64_t size = 0;
    std::optional<std::string> sha256;
};

// The body of /prepare-upload: who sends, and what.
struct PrepareUpload
{
    Device sender;
    std::vector<FileInfo> files;
};

// A transfer's progress, for the UI.
struct TransferUpdate
{
    std::string session;
    // "send" or "receive".
    std::string direction;
    // The file being carried, and how much of it is done.
    std::string file;
    uint64_t done = 0;
    uint64_t total = 0;
};

} // namespace cutefish::ripple

#endif // CUTEFISH_RIPPLE_PROTOCOL_H_
