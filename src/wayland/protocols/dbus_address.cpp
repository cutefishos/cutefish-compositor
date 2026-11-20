// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wayland/protocols/dbus_address.h"

#include <algorithm>
#include <cstddef>

namespace cutefish::wayland {
namespace {

bool isAsciiLetter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isAsciiDigit(char c)
{
    return c >= '0' && c <= '9';
}

template<typename Valid>
bool validElements(std::string_view text, char separator, size_t minimum, Valid valid)
{
    size_t count = 0;
    while (true) {
        const size_t end = text.find(separator);
        const std::string_view element = text.substr(0, end);
        if (element.empty() || !valid(element))
            return false;
        ++count;
        if (end == std::string_view::npos)
            return count >= minimum;
        text.remove_prefix(end + 1);
    }
}

} // namespace

bool validBusName(std::string_view name)
{
    const auto uniqueChar = [](char c) {
        return isAsciiLetter(c) || isAsciiDigit(c) || c == '_' || c == '-';
    };
    if (name.size() > 255)
        return false;
    if (!name.empty() && name.front() == ':') {
        return validElements(name.substr(1), '.', 2, [&](std::string_view element) {
            return std::all_of(element.begin(), element.end(), uniqueChar);
        });
    }
    return validElements(name, '.', 2, [&](std::string_view element) {
        return !isAsciiDigit(element.front())
                && std::all_of(element.begin(), element.end(), uniqueChar);
    });
}

bool validObjectPath(std::string_view path)
{
    if (path.empty() || path.front() != '/')
        return false;
    if (path.size() == 1)
        return true;
    return validElements(path.substr(1), '/', 1, [](std::string_view element) {
        return std::all_of(element.begin(), element.end(), [](char c) {
            return isAsciiLetter(c) || isAsciiDigit(c) || c == '_';
        });
    });
}

} // namespace cutefish::wayland
