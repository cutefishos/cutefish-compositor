// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ripple/protocol.h"

namespace cutefish::ripple {

using base::json::Value;

Value Device::toJson(bool announce) const
{
    Value value = Value::object();
    value.insert("alias", alias);
    value.insert("version", "2.0");
    value.insert("deviceModel", model);
    value.insert("deviceType", type);
    value.insert("fingerprint", fingerprint);
    value.insert("port", port);
    value.insert("protocol", protocol);
    value.insert("download", download);
    if (announce)
        value.insert("announce", true);
    return value;
}

std::optional<Device> Device::fromJson(const Value &value)
{
    if (!value.isObject())
        return std::nullopt;
    Device device;
    device.alias = value["alias"].toString();
    device.type = value["deviceType"].toString("desktop");
    device.model = value["deviceModel"].toString();
    device.fingerprint = value["fingerprint"].toString();
    device.protocol = value["protocol"].toString("https");
    const int64_t port = value["port"].toInt(53317);
    if (port > 0 && port < 65536)
        device.port = int(port);
    device.download = value["download"].toBool();
    if (device.fingerprint.empty())
        return std::nullopt;
    return device;
}

} // namespace cutefish::ripple
