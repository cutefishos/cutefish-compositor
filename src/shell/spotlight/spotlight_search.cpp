// SPDX-License-Identifier: BSD-3-Clause
#include "shell/spotlight/spotlight_search.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string_view>

#include "shell/common/text_match.h"

namespace cutefish::shell::spotlight {
namespace fs = std::filesystem;

namespace {

std::vector<std::string> words(const std::string &query)
{
    std::vector<std::string> result;
    std::istringstream input(toLower(query));
    for (std::string word; input >> word;)
        result.push_back(std::move(word));
    return result;
}

int score(std::string_view name, const std::vector<std::string> &terms, std::string_view extra = {})
{
    int rank = 0;
    for (const auto &word : terms) {
        if (name == word)
            rank += 12;
        else if (name.starts_with(word))
            rank += 8;
        else if (name.find(word) != name.npos)
            rank += 4;
        else if (extra.find(word) != extra.npos)
            ++rank;
        else
            return -1;
    }
    return rank;
}

std::pair<FileType, const char *> classify(const fs::path &path, bool directory)
{
    if (directory)
        return {FileType::Folders, "folder"};
    const auto ext = toLower(path.extension().string());
    const auto in = [&ext](std::initializer_list<std::string_view> values) {
        return std::find(values.begin(), values.end(), ext) != values.end();
    };
    if (ext == ".pdf")
        return {FileType::Pdf, "application-pdf"};
    if (in({".jpeg", ".jpg", ".png", ".svg", ".gif", ".webp", ".heic", ".heif", ".avif", ".arw", ".dng", ".tiff", ".bmp", ".cr2", ".nef"})) {
        const auto name = toLower(path.stem().string());
        return {name.starts_with("screenshot") || name.starts_with("screen shot") || name.starts_with("снимок экрана")
                        ? FileType::Screenshots : FileType::Images, "image-x-generic"};
    }
    if (in({".mp3", ".flac", ".m4a", ".wav", ".ogg", ".opus", ".aac"}))
        return {FileType::Audio, "audio-x-generic"};
    if (in({".mkv", ".mp4", ".mov", ".webm", ".avi", ".mpeg"}))
        return {FileType::Video, "video-x-generic"};
    if (in({".txt", ".md", ".rtf", ".doc", ".docx", ".odt", ".ods", ".xls", ".xlsx", ".csv", ".odp", ".pptx", ".epub"}))
        return {FileType::Documents, "text-x-generic"};
    return {FileType::All, "unknown"};
}

} // namespace

bool inCategory(const base::DesktopEntry &entry, Category category)
{
    const auto has = [&entry](std::initializer_list<std::string_view> values) {
        return std::any_of(entry.categories.begin(), entry.categories.end(), [&](const auto &value) {
            return std::find(values.begin(), values.end(), value) != values.end();
        });
    };
    switch (category) {
    case Category::All: return true;
    case Category::Utilities: return has({"Utility", "System", "Settings", "Development", "FileManager", "TerminalEmulator"});
    case Category::Productivity: return has({"Office", "Finance", "Calendar", "WordProcessor", "Spreadsheet", "ProjectManagement"});
    case Category::Creativity: return has({"Graphics", "Photography", "AudioVideoEditing", "AudioVideo", "Publishing"});
    case Category::Entertainment: return has({"Game", "AudioVideo", "Audio", "Video", "Player"});
    case Category::Social: return has({"Chat", "InstantMessaging", "IRCClient", "Email", "Telephony", "VideoConference"});
    case Category::Information: return has({"Education", "Science", "News", "WebBrowser", "Dictionary", "Viewer", "Documentation"});
    }
    return false;
}

std::vector<DesktopEntryPtr> findApps(const std::vector<DesktopEntryPtr> &apps, const std::string &query, Category category)
{
    auto terms = words(query);
    std::vector<std::pair<int, DesktopEntryPtr>> ranked;
    for (const auto &app : apps) {
        if (!inCategory(*app, category))
            continue;
        std::string extra = app->id + " " + app->genericName;
        for (const auto &key : app->keywords)
            extra += " " + key;
        const int rank = score(toLower(app->name), terms, toLower(extra));
        if (rank >= 0)
            ranked.emplace_back(rank, app);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) { return left.first > right.first; });
    std::vector<DesktopEntryPtr> result;
    result.reserve(ranked.size());
    for (auto &[rank, app] : ranked)
        result.push_back(std::move(app));
    return result;
}

std::vector<File> indexFiles(const fs::path &home)
{
    std::vector<File> index;
    if (home.empty() || !home.is_absolute() || home == home.root_path())
        return index;
    std::error_code error;
    constexpr auto options = fs::directory_options::skip_permission_denied;
    fs::recursive_directory_iterator cursor(home, options, error), end;
    constexpr size_t kVisitLimit = 35000;
    for (size_t visited = 0; !error && cursor != end && visited < kVisitLimit; ++visited, cursor.increment(error)) {
        const auto &path = cursor->path();
        const auto name = path.filename().string();
        std::error_code stat;
        if (name.starts_with('.') || name == "node_modules" || cursor.depth() >= 7 || cursor->is_symlink(stat)) {
            cursor.disable_recursion_pending();
            continue;
        }
        const bool directory = cursor->is_directory(stat);
        if (!directory && !cursor->is_regular_file(stat))
            continue;
        const auto [type, icon] = classify(path, directory);
        File file{path.string(), name, path.parent_path().string(), icon, type};
        file.foldedName = toLower(name);
        if (!directory) {
            const auto size = cursor->file_size(stat);
            if (!stat)
                file.size = size;
        }
        const auto modified = cursor->last_write_time(stat);
        if (!stat)
            file.modified = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(modified));
        index.push_back(std::move(file));
    }
    std::stable_sort(index.begin(), index.end(), [](const File &left, const File &right) { return left.modified > right.modified; });
    return index;
}

std::vector<const File *> findFiles(const std::vector<File> &index, const std::string &query, FileType type,
                                    const std::string &folder, size_t limit)
{
    if (limit == 0)
        return {};
    const auto terms = words(query);
    const std::string prefix = folder + '/';
    std::vector<std::pair<int, const File *>> ranked;
    for (const File &file : index) {
        if (!folder.empty() && !file.path.starts_with(prefix))
            continue;
        if (type != FileType::All && file.type != type
            && !(type == FileType::Images && file.type == FileType::Screenshots)
            && !(type == FileType::Documents && file.type == FileType::Pdf))
            continue;
        const int rank = terms.empty() ? 0 : score(file.foldedName, terms);
        if (rank >= 0)
            ranked.emplace_back(rank, &file);
        if (terms.empty() && ranked.size() == limit)
            break;
    }
    if (ranked.size() > limit) {
        std::partial_sort(ranked.begin(), ranked.begin() + limit, ranked.end(), [&index](auto a, auto b) {
            return a.first == b.first ? a.second - index.data() < b.second - index.data() : a.first > b.first;
        });
        ranked.resize(limit);
    } else {
        std::stable_sort(ranked.begin(), ranked.end(), [](auto a, auto b) { return a.first > b.first; });
    }
    std::vector<const File *> result;
    for (const auto &[rank, file] : ranked)
        result.push_back(file);
    return result;
}

} // namespace cutefish::shell::spotlight
