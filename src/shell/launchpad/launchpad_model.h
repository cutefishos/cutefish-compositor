// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_MODEL_H_
#define CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_MODEL_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/signal.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "shell/common/list_model.h"
#include "shell/common/synced_list.h"

namespace cutefish::shell {

class Applications;
class ApplicationLauncher;

struct LaunchpadItem {
    // A desktop id, or for a folder kFolderPrefix and its number.
    std::string id;
    std::string name;
    std::string genericName;
    std::string comment;
    std::string iconName;
    bool newInstalled = false;
    // Never empty for a folder; empty for an application.
    std::vector<LaunchpadItem> children;

    static constexpr const char *kFolderPrefix = "folder:";
    static bool isFolderId(const std::string &id) { return id.rfind(kFolderPrefix, 0) == 0; }
    bool isFolder() const { return isFolderId(id); }

    bool operator==(const LaunchpadItem &) const = default;
};

// Stored in the [Launchpad] group of shell.conf: order, newAppIds,
// Folder<n>/name and Folder<n>/apps.
class LaunchpadModel : public ListModelSignals
{
public:
    static constexpr std::chrono::milliseconds kSaveDelay{1000};

    // Both must outlive the model.
    LaunchpadModel(Applications &applications, ApplicationLauncher &launcher, std::string path);
    ~LaunchpadModel();

    LaunchpadModel(const LaunchpadModel &) = delete;
    LaunchpadModel &operator=(const LaunchpadModel &) = delete;

    const std::vector<LaunchpadItem> &rows() const { return m_rows.rows(); }
    int count() const { return int(m_rows.rows().size()); }
    const std::vector<LaunchpadItem> &allItems() const { return m_items; }

    void search(const std::string &key);
    // -1 for none, also for an application in a folder.
    int findById(const std::string &id) const;
    // Null when absent.
    const LaunchpadItem *item(const std::string &id) const;
    // Empty at the top level.
    std::string folderOf(const std::string &id) const;
    void clearNewInstalled(const std::vector<std::string> &ids);
    // Not while searching.
    void move(int from, int to);
    // A drag that ends without a drop puts the order back as it was.
    void beginReorder();
    void endReorder(bool keep);

    std::string createFolder(const std::string &id, const std::string &name);
    // Empty when nothing changed.
    std::string merge(const std::string &id, const std::string &target, const std::string &name);
    void removeFromFolder(const std::string &id);
    void moveInFolder(const std::string &folder, int from, int to);
    void renameFolder(const std::string &folder, const std::string &name);
    void ungroupFolder(const std::string &folder);

    void sendToDesktop(const std::string &key);

    bool launch(const std::string &idOrPath);
    bool launchRow(int row);

    void save();

    base::Signal<> applicationLaunched;

private:
    struct Stored {
        bool valid = false;
        std::vector<LaunchpadItem> items;
        std::vector<std::string> newAppIds;
    };
    struct Absent {
        LaunchpadItem item;
        // The folder it was in; empty at the top level.
        std::string folder;
        // Empty when it was first.
        std::string after;
    };

    static Stored load(const std::string &path);
    // False when its folder is not there.
    static bool placeAbsent(std::vector<LaunchpadItem> *items, const std::vector<Absent> &absent,
                            const Absent &record);
    void onApplicationsChanged();
    // False while either is still loading.
    bool reconcile();
    LaunchpadItem *findItem(const std::string &id);
    std::optional<LaunchpadItem> takeApplication(const std::string &id);
    std::vector<LaunchpadItem> itemsWithAbsent() const;
    std::string newFolderId() const;
    void changed();
    void updateRows();
    void delaySave();

    Applications &m_applications;
    ApplicationLauncher &m_launcher;
    std::string m_path;
    std::vector<LaunchpadItem> m_items;
    std::vector<Absent> m_absent;
    SyncedList<LaunchpadItem> m_rows;
    std::string m_searchKey;
    std::set<std::string> m_openedUnknownIds;
    bool m_loaded = false;
    bool m_firstLoad = false;
    base::Timer m_saveTimer;
    std::optional<std::vector<LaunchpadItem>> m_reorderBase;
    bool m_saveDueBeforeReorder = false;
    bool m_savePending = false;
    base::Job m_load;
    base::Subscription m_applicationsChanged;
};

class LaunchpadPageModel : public ListModelSignals
{
public:
    explicit LaunchpadPageModel(LaunchpadModel &source);

    int startIndex() const { return m_startIndex; }
    void setStartIndex(int startIndex);
    int limitCount() const { return m_limitCount; }
    void setLimitCount(int limitCount);

    int count() const { return int(m_rows.rows().size()); }
    int sourceRow(int row) const;
    const LaunchpadItem *item(int row) const;

private:
    void sync();

    LaunchpadModel &m_source;
    SyncedList<std::string> m_rows;
    int m_startIndex = 0;
    int m_limitCount = 0;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHPAD_LAUNCHPAD_MODEL_H_
