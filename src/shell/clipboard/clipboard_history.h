// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_HISTORY_H_
#define CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_HISTORY_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/thread_pool.h"
#include "shell/clipboard/clipboard_monitor.h"
#include "shell/clipboard/clipboard_store.h"
#include "shell/common/list_model.h"

namespace cutefish::clipboard {
class SelectionController;
} // namespace cutefish::clipboard

namespace cutefish::shell {

class ClipboardHistoryModel : public ListModelSignals
{
public:
    explicit ClipboardHistoryModel(ClipboardStore &store);

    int count() const { return int(m_rows.size()); }
    const ClipboardEntry &entry(int row) const;
    std::string keyAt(int row) const;

    void setFilterText(const std::string &text);
    bool historyEmpty() const { return m_store.entries().empty(); }

    // "text", "image" or "files".
    static std::string kind(const ClipboardEntry &entry);
    // One line: the text's start, "Image, <time>", a file's name or
    // "<n> Files".
    std::string title(int row) const;
    std::string detail(int row) const;
    // Empty for others, or until known.
    std::string iconName(int row) const;

private:
    void rebuild();
    void lookUpIcons();

    ClipboardStore &m_store;
    std::string m_filterText;
    // Indices into the store's entries.
    std::vector<size_t> m_rows;
    std::map<std::string, std::string> m_icons;
    base::Job m_iconJob;
    base::Subscription m_changed;
};

class ClipboardHistory
{
public:
    ClipboardHistory(base::dbus::Connection *bus, clipboard::SelectionController &selection,
                     std::string directory = ClipboardStore::defaultDirectory(), size_t limit = 200);
    ~ClipboardHistory();

    ClipboardHistory(const ClipboardHistory &) = delete;
    ClipboardHistory &operator=(const ClipboardHistory &) = delete;

    bool registerService();

    ClipboardStore &store() { return m_store; }
    ClipboardHistoryModel &model() { return m_model; }

    std::function<void()> togglePicker;
    std::function<void()> closePicker;
    // Waits for the keys the user still holds.
    std::function<void()> sendPaste;

    // The body is read first, without blocking.
    void paste(int row);
    void copy(int row);
    void remove(int row);
    void clear();
    void toggle();

private:
    void select(int row, std::function<void()> then);

    base::dbus::Connection *m_bus;
    ClipboardStore m_store;
    ClipboardMonitor m_monitor;
    ClipboardHistoryModel m_model;
    std::unique_ptr<base::dbus::Object> m_object;
    std::vector<base::Subscription> m_subscriptions;
    // Selections being read; a later one supersedes an earlier.
    uint64_t m_selectSerial = 0;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_HISTORY_H_
