// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_MONITOR_H_
#define CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_MONITOR_H_

#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"
#include "base/timer.h"
#include "shell/clipboard/clipboard_store.h"

namespace cutefish::clipboard {
class DataSource;
class SelectionController;
} // namespace cutefish::clipboard

namespace cutefish::shell {

// A newer selection cancels a read in progress, so a program that writes the
// clipboard twice in a row leaves one copy.
class ClipboardMonitor
{
public:
    static constexpr std::chrono::milliseconds kReadDeadline{2000};
    // Carries the history entry's key, so the selection coming back is known.
    static constexpr const char *kEntryType = "application/x-cutefish-clipboard-history";
    // cutefish-services takes over every selection to keep it after its owner
    // quits, except one that carries this.
    static constexpr const char *kServicesType = "application/x-cutefish-clipboard";

    explicit ClipboardMonitor(clipboard::SelectionController &selection);
    ~ClipboardMonitor();

    ClipboardMonitor(const ClipboardMonitor &) = delete;
    ClipboardMonitor &operator=(const ClipboardMonitor &) = delete;

    // `key` names the history entry, so the selection coming back is recognised.
    bool setSelection(const ClipboardPayload &payload, const std::string &key);

    base::Signal<const ClipboardPayload &> copied;
    base::Signal<const std::string &> ownSelectionShown;

private:
    void onSelectionChanged();
    void readNext();
    void onReadable();
    void finishFormat();
    void cancelRead();

    clipboard::SelectionController &m_selection;
    base::Subscription m_changed;
    std::weak_ptr<clipboard::DataSource> m_source;
    std::vector<std::string> m_pendingFormats;
    std::string m_readingFormat;
    std::string m_buffer;
    ClipboardPayload m_payload;
    int64_t m_readSize = 0;
    int m_readFd = -1;
    int m_readWatch = 0;
    base::Timer m_deadline;
    std::string m_ownKey;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CLIPBOARD_CLIPBOARD_MONITOR_H_
