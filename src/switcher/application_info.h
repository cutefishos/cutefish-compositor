// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SWITCHER_APPLICATION_INFO_H_
#define CUTEFISH_SWITCHER_APPLICATION_INFO_H_

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/lru_cache.h"

namespace cutefish::switcher {

inline const std::string kFallbackIcon = "application-x-desktop";

struct ApplicationInfo {
    std::string name;
    // An icon theme name, or an absolute path.
    std::string icon;
};

// Answers, found or not, are kept until an application directory changes.
class ApplicationInfoCache
{
public:
    // The user's applications directory first.
    ApplicationInfoCache();
    explicit ApplicationInfoCache(std::vector<std::string> directories);

    // Checked once per list rather than once per entry: it reads each directory's time.
    void refresh();

    // `fallbackName` names an application with no desktop entry.
    ApplicationInfo lookup(const std::string &applicationId, const std::string &fallbackName);

private:
    const std::unordered_map<std::string, std::string> &wmClasses();
    int64_t applicationsStamp() const;

    std::vector<std::string> m_directories;
    int64_t m_stamp = -1;
    // Bounded: application ids come from clients, and misses are cached too.
    base::LruCache<std::string, ApplicationInfo> m_cache {256};
    std::optional<std::unordered_map<std::string, std::string>> m_wmClasses;
};

} // namespace cutefish::switcher

#endif // CUTEFISH_SWITCHER_APPLICATION_INFO_H_
