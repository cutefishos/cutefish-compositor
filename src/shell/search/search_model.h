// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SEARCH_SEARCH_MODEL_H_
#define CUTEFISH_SHELL_SEARCH_SEARCH_MODEL_H_

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/signal.h"
#include "base/timer.h"
#include "cutefish/files/directory_lister.h"
#include "shell/search/search_answer.h"

namespace cutefish::base {
class Subscription;
class EventLoop;
} // namespace cutefish::base

namespace cutefish::shell {

// One row of the search: an application to start, or a file to open.
struct SearchResult
{
    enum class Kind {
        Application,
        File,
        // A calculation or a conversion, answered in place.
        Answer,
    };

    Kind kind = Kind::Application;
    // The application's name, or the file's name.
    std::string title;
    // The application's description, or the folder a file sits in.
    std::string subtitle;
    // The application's icon name; empty for a file.
    std::string icon;
    // The desktop entry's id, for an application.
    std::string appId;
    // The local path, for a file.
    std::string path;
    bool isDirectory = false;

    bool operator==(const SearchResult &) const = default;
};

// The desktop's search: applications from the registry, files from the same
// lister the file manager searches with, both while the words are typed.
class SearchModel
{
public:
    SearchModel();
    ~SearchModel();

    SearchModel(const SearchModel &) = delete;
    SearchModel &operator=(const SearchModel &) = delete;

    // Empty clears; anything else debounces, then searches as the words are.
    void setQuery(const std::string &text);
    // Cancels any pending debounce or in-flight file walk.
    void stop();
    const std::string &query() const { return m_query; }

    static bool isIgnoredPath(std::string_view path, std::string_view root = {});

    // Applications first, then files.
    const std::vector<SearchResult> &results() const { return m_results; }
    // The rest of the best-matching name shown ahead of the caret. Empty when
    // nothing completes the words.
    const std::string &completion() const { return m_completion; }

    // True while a file search is still handing results on.
    bool searching() const { return m_searching; }

    base::Signal<> changed;

    static constexpr int kMaxApplications = 6;
    static constexpr int kMaxFiles = 24;

private:
    void run();
    void takeFiles();
    void rebuild();
    // A file search that found nothing says so once it is done, not before.
    void setSearching(bool searching);

    base::EventLoop &m_loop;
    base::Timer m_debounce;
    files::DirectoryLister m_lister;
    base::Subscription m_itemsChanged;
    base::Subscription m_completed;
    base::Subscription m_canceled;
    std::string m_query;
    std::optional<SearchAnswer> m_answer;
    std::vector<SearchResult> m_applications;
    std::vector<SearchResult> m_files;
    std::string m_completion;
    bool m_searching = false;
    std::vector<SearchResult> m_results;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SEARCH_SEARCH_MODEL_H_
