// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/search/search_model.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "base/application_registry.h"
#include "base/paths.h"
#include "core/main_loop.h"
#include "cutefish/files/file_item.h"
#include "cutefish/files/url.h"

namespace cutefish::shell {

namespace {

bool containsIgnoringCase(std::string_view text, std::string_view needle)
{
    if (needle.empty())
        return true;
    const auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                                [](char left, char right) {
                                    return std::tolower(static_cast<unsigned char>(left))
                                            == std::tolower(static_cast<unsigned char>(right));
                                });
    return it != text.end();
}

std::vector<std::string> wordsOf(const std::string &text)
{
    std::vector<std::string> words;
    std::string current;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty())
                words.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty())
        words.push_back(std::move(current));
    return words;
}

// Every word has to be in the text, as the file manager's own search asks.
bool matchesAll(const std::string &text, const std::vector<std::string> &words)
{
    for (const std::string &word : words) {
        if (!containsIgnoringCase(text, word))
            return false;
    }
    return true;
}

bool startsWithIgnoringCase(std::string_view text, std::string_view prefix)
{
    if (prefix.size() > text.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i]))
            != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    }
    return true;
}

// How well an application answers the words typed: 0 when its name starts
// with them, 1 when its name holds them, 2 from its generic name or comment.
// -1 when it does not answer them at all. Ids are left out: every Cutefish
// entry is called "cutefish-…", so "cut" would answer the whole desktop.
int applicationRank(const base::DesktopEntry &entry, const std::string &query,
                    const std::vector<std::string> &words)
{
    if (startsWithIgnoringCase(entry.name, query))
        return 0;
    if (containsIgnoringCase(entry.name, query))
        return 1;
    if (matchesAll(entry.name, words))
        return 2;
    const std::string details = entry.genericName + " " + entry.comment;
    return matchesAll(details, words) ? 3 : -1;
}

std::string shortenedPath(const std::string &path)
{
    const std::string home = base::paths::home();
    if (path.size() < home.size() || path.compare(0, home.size(), home) != 0)
        return path;
    return "~" + path.substr(home.size());
}

std::string folderOf(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : shortenedPath(path.substr(0, slash));
}

} // namespace

SearchModel::SearchModel()
    : m_loop(*core::mainLoop())
    , m_debounce(&m_loop)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(std::chrono::milliseconds(120));
    m_debounce.onTimeout = [this] { run(); };

    m_lister.setShowHiddenFiles(false);
    m_lister.setAutoUpdate(false);
    m_itemsChanged = m_lister.itemsChanged.connect([this] { takeFiles(); });
    m_completed = m_lister.completed.connect([this] { setSearching(false); });
    m_canceled = m_lister.canceled.connect([this] { setSearching(false); });
}

SearchModel::~SearchModel() = default;

void SearchModel::setQuery(const std::string &text)
{
    std::string trimmed;
    bool space = false;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            space = !trimmed.empty();
            continue;
        }
        if (space)
            trimmed.push_back(' ');
        space = false;
        trimmed.push_back(c);
    }
    if (trimmed == m_query)
        return;
    m_query = std::move(trimmed);
    m_debounce.stop();
    m_lister.stop();

    if (m_query.empty()) {
        m_answer.reset();
        m_applications.clear();
        m_files.clear();
        // Reset the lister's last search words so re-typing the same query runs again.
        m_lister.search({}, {});
        setSearching(false);
        rebuild();
        return;
    }
    setSearching(true);
    m_debounce.start();
}

void SearchModel::run()
{
    const std::vector<std::string> words = wordsOf(m_query);
    m_answer = answerFor(m_query);

    struct Match
    {
        int rank;
        SearchResult result;
    };
    std::vector<Match> matches;
    if (!words.empty()) {
        const auto &applications = base::ApplicationRegistry::instance().applications();
        for (const auto &entry : applications) {
            if (!entry || entry->name.empty())
                continue;
            const int rank = applicationRank(*entry, m_query, words);
            if (rank < 0)
                continue;
            matches.push_back({rank, {.kind = SearchResult::Kind::Application,
                                      .title = entry->name,
                                      .subtitle = entry->genericName.empty() ? entry->comment
                                                                             : entry->genericName,
                                      .icon = entry->icon,
                                      .appId = entry->id}});
        }
    }
    // A name that starts with the words first, the rest as they were.
    std::stable_sort(matches.begin(), matches.end(),
                     [](const Match &left, const Match &right) { return left.rank < right.rank; });
    m_applications.clear();
    for (const Match &match : matches) {
        m_applications.push_back(match.result);
        if (int(m_applications.size()) >= kMaxApplications)
            break;
    }
    m_files.clear();
    rebuild();

    if (words.empty()) {
        m_lister.stop();
        setSearching(false);
        return;
    }
    setSearching(true);
    m_lister.search(files::Url::fromLocalFile(base::paths::home()), m_query);
}

void SearchModel::setSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    changed();
}

void SearchModel::takeFiles()
{
    // The listing the lister had open before the search is not an answer.
    if (!m_lister.isSearching()) {
        m_files.clear();
        return;
    }
    m_files.clear();
    const files::FileItemList &items = m_lister.items();
    for (const files::FileItem &item : items) {
        if (item.isHidden())
            continue;
        const std::string path = item.localPath();
        if (path.empty() || path == base::paths::home())
            continue;
        m_files.push_back({.kind = SearchResult::Kind::File,
                           .title = item.name(),
                           .subtitle = folderOf(path),
                           .path = path,
                           .isDirectory = item.isDir()});
        if (int(m_files.size()) >= kMaxFiles)
            break;
    }
    rebuild();
}

void SearchModel::rebuild()
{
    std::vector<SearchResult> results;
    results.reserve(1 + m_applications.size() + m_files.size());
    if (m_answer)
        results.push_back({.kind = SearchResult::Kind::Answer, .title = m_answer->value,
                           .subtitle = m_answer->expression});
    results.insert(results.end(), m_applications.begin(), m_applications.end());
    results.insert(results.end(), m_files.begin(), m_files.end());
    // The rest of the best-matching name; an exact match offers nothing.
    std::string completion;
    if (!m_query.empty()) {
        const auto top = std::find_if(results.begin(), results.end(), [](const SearchResult &result) {
            return result.kind != SearchResult::Kind::Answer;
        });
        if (top != results.end() && top->title.size() > m_query.size()
            && startsWithIgnoringCase(top->title, m_query))
            completion = top->title.substr(m_query.size());
    }
    const bool completionChanged = completion != m_completion;
    m_completion = std::move(completion);

    if (results == m_results && !completionChanged)
        return;
    m_results = std::move(results);
    changed();
}

} // namespace cutefish::shell
