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
    // Installed since the launchpad last looked, until it is opened; for a
    // folder, whether any of its applications is.
    bool newInstalled = false;
    // A folder's applications in its order, never empty; none for an
    // application.
    std::vector<LaunchpadItem> children;

    static constexpr const char *kFolderPrefix = "folder:";
    static bool isFolderId(const std::string &id) { return id.rfind(kFolderPrefix, 0) == 0; }
    bool isFolder() const { return isFolderId(id); }

    bool operator==(const LaunchpadItem &) const = default;
};

// The launchpad's applications and folders in the user's order, or the
// applications a search finds: the order in the [Launchpad] group of
// shell.conf (order, the desktop ids and folder ids, newAppIds, and for each
// folder Folder<n>/name and Folder<n>/apps), new applications appended and
// marked, alphabetical the first time. A search keeps that order, looks into
// the folders and matches the name, generic name, comment or id, ignoring
// case. An application whose entry goes away, as during a package upgrade,
// leaves the rows at once but keeps its place and folder until the next
// login, and comes back there if the entry returns.
class LaunchpadModel : public ListModelSignals
{
public:
    static constexpr std::chrono::milliseconds kSaveDelay{1000};

    // `path` is shell.conf. Both must outlive the model.
    LaunchpadModel(Applications &applications, ApplicationLauncher &launcher, std::string path);
    ~LaunchpadModel();

    LaunchpadModel(const LaunchpadModel &) = delete;
    LaunchpadModel &operator=(const LaunchpadModel &) = delete;

    // The rows: every application, or the search's results.
    const std::vector<LaunchpadItem> &rows() const { return m_rows.rows(); }
    int count() const { return int(m_rows.rows().size()); }
    // Every application and folder in the user's order.
    const std::vector<LaunchpadItem> &allItems() const { return m_items; }

    void search(const std::string &key);
    // The index in allItems() of a desktop id or path, or of a folder id;
    // -1, also for an application in a folder.
    int findById(const std::string &id) const;
    // An application wherever it is, or a folder; null when absent.
    const LaunchpadItem *item(const std::string &id) const;
    // The folder an application is in; empty at the top level.
    std::string folderOf(const std::string &id) const;
    // Applications opened outside the launchpad are no longer new either.
    void clearNewInstalled(const std::vector<std::string> &ids);
    // Within allItems(); not while searching.
    void move(int from, int to);
    // A drag's moves are not saved while it runs, and a drag that ends
    // without a drop puts the order back as it was.
    void beginReorder();
    void endReorder(bool keep);

    // --- Folders; none while searching ---

    // A new folder in the application's place, holding it. Returns its id.
    std::string createFolder(const std::string &id, const std::string &name);
    // Drops an application on another, or on a folder: the other and it
    // become a new folder in the other's place, or it joins the folder at
    // its end. Returns the folder's id; empty when nothing changed, as for
    // an application dropped on its own folder.
    std::string merge(const std::string &id, const std::string &target, const std::string &name);
    // Out of its folder, to the top level just after it. A folder left
    // with one application gives way to it, and one left empty goes. Only
    // a folder just made from the menu holds a single application.
    void removeFromFolder(const std::string &id);
    // Within a folder's applications.
    void moveInFolder(const std::string &folder, int from, int to);
    void renameFolder(const std::string &folder, const std::string &name);
    // The folder's applications take its place.
    void ungroupFolder(const std::string &folder);

    // Links the entry's file onto the desktop.
    void sendToDesktop(const std::string &key);

    bool launch(const std::string &idOrPath);
    bool launchRow(int row);

    // Writes the order now; otherwise it goes a second after the last change.
    void save();

    base::Signal<> applicationLaunched;

private:
    struct Stored {
        bool valid = false;
        std::vector<LaunchpadItem> items;
        std::vector<std::string> newAppIds;
    };
    // An application shown this session whose entry is gone, or a folder
    // whose applications all are; a folder's record holds no children.
    struct Absent {
        LaunchpadItem item;
        // The folder it was in; empty at the top level.
        std::string folder;
        // The item before it there; empty when it was first.
        std::string after;
    };

    static Stored load(const std::string &path);
    // Puts a record back among `items`, after the item it followed or, while
    // that one is absent too, after where that one goes; at the end when
    // the item before is gone for good. False when its folder is not there.
    static bool placeAbsent(std::vector<LaunchpadItem> *items, const std::vector<Absent> &absent,
                            const Absent &record);
    void onApplicationsChanged();
    // Brings the items up to date with the applications, without showing
    // or saving them; false while either is still loading.
    bool reconcile();
    LaunchpadItem *findItem(const std::string &id);
    // Takes an application out of the top level or its folder; a folder
    // left with one application gives way to it, and one left empty goes.
    std::optional<LaunchpadItem> takeApplication(const std::string &id);
    // The items with every absent one back in its place.
    std::vector<LaunchpadItem> itemsWithAbsent() const;
    std::string newFolderId() const;
    void changed();
    void updateRows();
    void delaySave();

    Applications &m_applications;
    ApplicationLauncher &m_launcher;
    std::string m_path;
    std::vector<LaunchpadItem> m_items;
    // In the order they went.
    std::vector<Absent> m_absent;
    SyncedList<LaunchpadItem> m_rows;
    std::string m_searchKey;
    // Opened before the registry listed them; they do not arrive as new.
    std::set<std::string> m_openedUnknownIds;
    bool m_loaded = false;
    bool m_firstLoad = false;
    base::Timer m_saveTimer;
    // The items when a drag began, while it runs.
    std::optional<std::vector<LaunchpadItem>> m_reorderBase;
    bool m_saveDueBeforeReorder = false;
    bool m_savePending = false;
    base::Job m_load;
    base::Subscription m_applicationsChanged;
};

// A page of the launchpad's grid: the source's rows from startIndex(), at
// most limitCount() of them, kept by id so a page shows the same icons
// across changes elsewhere in the list.
class LaunchpadPageModel : public ListModelSignals
{
public:
    explicit LaunchpadPageModel(LaunchpadModel &source);

    int startIndex() const { return m_startIndex; }
    void setStartIndex(int startIndex);
    int limitCount() const { return m_limitCount; }
    void setLimitCount(int limitCount);

    int count() const { return int(m_rows.rows().size()); }
    // The source row of a page row.
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
