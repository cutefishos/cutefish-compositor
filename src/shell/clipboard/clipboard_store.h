// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_STORE_H_
#define CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_STORE_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/signal.h"
#include "base/thread_pool.h"

namespace cutefish::shell {

// What one copy held: each kept type with its bytes, in the order kept.
using ClipboardPayload = std::vector<std::pair<std::string, std::string>>;

struct ClipboardEntry {
    enum class Kind {
        Text = 0,
        Image = 1,
        Files = 2,
    };

    // The SHA-256 of the payload, in hex; also the name of its body file.
    std::string key;
    Kind kind = Kind::Text;
    // Milliseconds since the epoch.
    std::optional<int64_t> copied;
    std::string text;
    std::vector<std::string> files;
    std::string imageType;
};

// Every read and write runs on the file thread; the entries here are what the
// index says once a change is through.
class ClipboardStore
{
public:
    static constexpr int64_t kMaxPayloadSize = 16 * 1024 * 1024;
    // In UTF-16 units.
    static constexpr size_t kIndexedTextLength = 8192;

    explicit ClipboardStore(std::string directory = defaultDirectory(), size_t limit = 200);
    ~ClipboardStore();

    ClipboardStore(const ClipboardStore &) = delete;
    ClipboardStore &operator=(const ClipboardStore &) = delete;

    static std::string defaultDirectory();

    // Empty when nothing should be, a password manager's copy for one.
    static std::vector<std::string> formatsToKeep(const std::vector<std::string> &offered);
    // False when it holds nothing worth keeping.
    static bool describe(const ClipboardPayload &payload, ClipboardEntry *entry);
    static std::string keyFor(const ClipboardPayload &payload);

    bool isLoaded() const { return m_loaded; }
    const std::vector<ClipboardEntry> &entries() const { return m_entries; }
    int indexOf(const std::string &key) const;

    // False when it is not kept.
    bool add(const ClipboardPayload &payload, std::optional<int64_t> copied = std::nullopt);
    void moveToTop(const std::string &key);
    void remove(const std::string &key);
    void clear();

    // An empty payload when it cannot.
    void payload(const std::string &key, std::function<void(ClipboardPayload)> done);
    static ClipboardPayload readBody(const std::string &path);
    std::string bodyPath(const std::string &key) const;

    base::Signal<> changed;

private:
    void save();

    std::string m_directory;
    size_t m_limit;
    bool m_loaded = false;
    std::vector<ClipboardEntry> m_entries;
    std::set<std::string> m_writing;
    base::Job m_load;
    std::vector<base::Job> m_jobs;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_STORE_H_
