// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "shell/common/applications.h"

namespace cutefish::shell::spotlight {

enum class Category { All, Utilities, Productivity, Creativity, Entertainment, Social, Information };
enum class FileType { All, Folders, Documents, Pdf, Images, Screenshots, Audio, Video };

struct File {
    std::string path;
    std::string name;
    std::string parent;
    std::string icon;
    FileType type;
    uintmax_t size = 0;
    int64_t modified = 0;
    std::string foldedName;
};

bool inCategory(const base::DesktopEntry &entry, Category category);
std::vector<DesktopEntryPtr> findApps(const std::vector<DesktopEntryPtr> &apps, const std::string &query,
                                      Category category = Category::All);
std::vector<File> indexFiles(const std::filesystem::path &home);
std::vector<const File *> findFiles(const std::vector<File> &index, const std::string &query, FileType type,
                                    const std::string &folder, size_t limit);

} // namespace cutefish::shell::spotlight
