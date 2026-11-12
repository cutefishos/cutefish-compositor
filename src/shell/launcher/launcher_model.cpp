// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/launcher/launcher_model.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <tuple>
#include <unistd.h>
#include <utility>

#include "base/locale.h"
#include "base/paths.h"
#include "base/settings.h"
#include "base/strings.h"
#include "shell/common/app_launcher.h"
#include "shell/common/applications.h"
#include "shell/common/file_thread.h"
#include "shell/common/text_match.h"

namespace cutefish::shell {

static void updateItem(LauncherItem *item, const base::DesktopEntry &entry)
{
    item->id = entry.id;
    item->name = entry.name;
    item->genericName = entry.genericName;
    item->comment = entry.comment;
    item->iconName = entry.icon;
}

static std::string absolutePath(const std::string &path)
{
    std::error_code error;
    return std::filesystem::absolute(path, error).lexically_normal().string();
}

// A folder's number, as its group in shell.conf is named.
static std::string folderNumber(const std::string &id)
{
    return id.substr(std::strlen(LauncherItem::kFolderPrefix));
}

LauncherModel::LauncherModel(Applications &applications, ApplicationLauncher &launcher, std::string path)
    : m_applications(applications)
    , m_launcher(launcher)
    , m_path(std::move(path))
    , m_rows(*this, [](const LauncherItem &item) { return item.id; },
             [](const LauncherItem &a, const LauncherItem &b) {
                 return a.name == b.name && a.genericName == b.genericName && a.comment == b.comment
                         && a.iconName == b.iconName && a.newInstalled == b.newInstalled
                         && std::equal(a.children.begin(), a.children.end(), b.children.begin(),
                                       b.children.end(), [](const LauncherItem &x, const LauncherItem &y) {
                                           return x.id == y.id && x.name == y.name && x.iconName == y.iconName
                                                   && x.newInstalled == y.newInstalled;
                                       });
             })
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.onTimeout = [this] { save(); };

    m_applicationsChanged = m_applications.changed.connect([this] { onApplicationsChanged(); });
    m_load = runOnFileThread([path = m_path] { return load(path); },
                             [this](Stored stored) {
                                 m_firstLoad = !stored.valid;
                                 if (stored.valid) {
                                     m_items = std::move(stored.items);
                                     const auto isNew = [&stored](const LauncherItem &item) {
                                         return std::find(stored.newAppIds.begin(), stored.newAppIds.end(),
                                                          item.id)
                                                 != stored.newAppIds.end();
                                     };
                                     for (LauncherItem &item : m_items) {
                                         item.newInstalled = isNew(item);
                                         for (LauncherItem &child : item.children)
                                             child.newInstalled = isNew(child);
                                     }
                                 }
                                 m_loaded = true;
                                 onApplicationsChanged();
                             });
}

LauncherModel::~LauncherModel()
{
    save();
    waitForFileThread();
}

LauncherModel::Stored LauncherModel::load(const std::string &path)
{
    const base::Settings settings(path);
    Stored stored;
    if (!settings.contains("Launcher/order"))
        return stored;

    // Names and icons come from the entries; only the ids are kept, and a
    // folder's name.
    for (const std::string &id : settings.stringListValue("Launcher/order")) {
        LauncherItem item;
        item.id = id;
        if (item.isFolder()) {
            const std::string group = "Launcher/Folder" + folderNumber(id);
            item.name = settings.stringValue(group + "/name");
            for (const std::string &child : settings.stringListValue(group + "/apps")) {
                LauncherItem app;
                app.id = child;
                item.children.push_back(std::move(app));
            }
        }
        stored.items.push_back(std::move(item));
    }
    stored.valid = true;
    stored.newAppIds = settings.stringListValue("Launcher/newAppIds");
    return stored;
}

void LauncherModel::save()
{
    // A shutdown before the first scan must not set a baseline.
    if (!m_loaded || m_firstLoad)
        return;
    m_saveTimer.stop();

    std::vector<std::string> order;
    std::vector<std::string> newAppIds;
    // Each folder's number, name and applications.
    std::vector<std::tuple<std::string, std::string, std::vector<std::string>>> folders;
    for (const LauncherItem &item : itemsWithAbsent()) {
        order.push_back(item.id);
        if (!item.isFolder()) {
            if (item.newInstalled)
                newAppIds.push_back(item.id);
            continue;
        }
        std::vector<std::string> apps;
        for (const LauncherItem &child : item.children) {
            apps.push_back(child.id);
            if (child.newInstalled)
                newAppIds.push_back(child.id);
        }
        folders.emplace_back(folderNumber(item.id), item.name, std::move(apps));
    }

    fileThread().post([path = m_path, order, newAppIds, folders] {
        base::Settings settings(path);
        settings.remove("Launcher");
        settings.setValue("Launcher/order", order);
        settings.setValue("Launcher/newAppIds", newAppIds);
        for (const auto &[number, name, apps] : folders) {
            settings.setValue("Launcher/Folder" + number + "/name", name);
            settings.setValue("Launcher/Folder" + number + "/apps", apps);
        }
        settings.sync();
    });
}

void LauncherModel::delaySave()
{
    if (m_reorderBase) {
        m_savePending = true;
        return;
    }
    m_saveTimer.start(kSaveDelay);
}

void LauncherModel::beginReorder()
{
    m_reorderBase = m_items;
    // A save already due would write the drag's moves.
    m_saveDueBeforeReorder = m_saveTimer.isActive();
    m_saveTimer.stop();
    m_savePending = false;
}

void LauncherModel::endReorder(bool keep)
{
    if (!m_reorderBase)
        return;
    std::vector<LauncherItem> base = *std::exchange(m_reorderBase, std::nullopt);
    bool save = std::exchange(m_saveDueBeforeReorder, false);
    if (keep) {
        save = std::exchange(m_savePending, false) || save;
    } else {
        // What was saved, or due to be; the drag's own changes, which ask
        // for a save as they happen, are dropped with it.
        const std::vector<LauncherItem> before = base;
        m_savePending = false;
        // The order and folders as they were, with what the applications
        // became during the drag: those that arrived stay after the others,
        // and those opened are no longer new.
        std::map<std::string, bool> newInstalled;
        for (const LauncherItem &item : m_items) {
            newInstalled[item.id] = item.newInstalled;
            for (const LauncherItem &child : item.children)
                newInstalled[child.id] = child.newInstalled;
        }
        std::set<std::string> seen;
        for (LauncherItem &item : base) {
            for (LauncherItem &child : item.children) {
                if (const auto it = newInstalled.find(child.id); it != newInstalled.end())
                    child.newInstalled = it->second;
                seen.insert(child.id);
            }
            if (const auto it = newInstalled.find(item.id); it != newInstalled.end() && !item.isFolder())
                item.newInstalled = it->second;
            seen.insert(item.id);
        }
        for (const LauncherItem &item : m_items) {
            if (!item.isFolder() && !seen.count(item.id))
                base.push_back(item);
        }
        m_items = std::move(base);
        reconcile();
        updateRows();
        save = save || m_items != before;
    }
    if (save)
        delaySave();
}

void LauncherModel::search(const std::string &key)
{
    const std::string trimmed(base::strings::trimmed(key));
    if (trimmed == m_searchKey)
        return;
    m_searchKey = trimmed;
    updateRows();
}

void LauncherModel::updateRows()
{
    if (m_searchKey.empty()) {
        m_rows.sync(m_items);
        return;
    }
    std::vector<LauncherItem> results;
    const auto match = [this, &results](const LauncherItem &item) {
        if (containsIgnoringCase(item.name, m_searchKey) || containsIgnoringCase(item.genericName, m_searchKey)
            || containsIgnoringCase(item.comment, m_searchKey) || containsIgnoringCase(item.id, m_searchKey))
            results.push_back(item);
    };
    for (const LauncherItem &item : m_items) {
        if (!item.isFolder())
            match(item);
        for (const LauncherItem &child : item.children)
            match(child);
    }
    m_rows.sync(results);
}

int LauncherModel::findById(const std::string &id) const
{
    std::string canonicalId = id;
    if (const DesktopEntryPtr entry = m_applications.byPath(id))
        canonicalId = entry->id;
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == canonicalId)
            return int(i);
    }
    return -1;
}

void LauncherModel::clearNewInstalled(const std::vector<std::string> &ids)
{
    bool changed = false;
    for (const std::string &id : ids) {
        std::string canonicalId = id;
        if (const DesktopEntryPtr entry = m_applications.byPath(id))
            canonicalId = entry->id;
        LauncherItem *item = LauncherItem::isFolderId(canonicalId) ? nullptr : findItem(canonicalId);
        if (!item) {
            m_openedUnknownIds.insert(id);
            continue;
        }
        if (item->newInstalled) {
            item->newInstalled = false;
            changed = true;
        }
    }
    if (changed)
        this->changed();
}

void LauncherModel::move(int from, int to)
{
    // Indices are into the whole list, which the rows are only outside a
    // search.
    if (from == to || !m_searchKey.empty())
        return;
    if (from < 0 || to < 0 || size_t(from) >= m_items.size() || size_t(to) >= m_items.size())
        return;
    LauncherItem item = std::move(m_items[size_t(from)]);
    m_items.erase(m_items.begin() + from);
    m_items.insert(m_items.begin() + to, std::move(item));
    updateRows();
    delaySave();
}

const LauncherItem *LauncherModel::item(const std::string &id) const
{
    for (const LauncherItem &item : m_items) {
        if (item.id == id)
            return &item;
        for (const LauncherItem &child : item.children) {
            if (child.id == id)
                return &child;
        }
    }
    return nullptr;
}

LauncherItem *LauncherModel::findItem(const std::string &id)
{
    return const_cast<LauncherItem *>(std::as_const(*this).item(id));
}

std::string LauncherModel::folderOf(const std::string &id) const
{
    for (const LauncherItem &item : m_items) {
        for (const LauncherItem &child : item.children) {
            if (child.id == id)
                return item.id;
        }
    }
    return {};
}

std::string LauncherModel::newFolderId() const
{
    // One past the highest number in use, also by the order a drag may
    // put back and by a hidden folder.
    int highest = 0;
    const auto note = [&highest](const std::vector<LauncherItem> &items) {
        for (const LauncherItem &item : items) {
            if (item.isFolder())
                highest = std::max(highest, base::strings::toNumber<int>(folderNumber(item.id)).value_or(0));
        }
    };
    note(m_items);
    if (m_reorderBase)
        note(*m_reorderBase);
    for (const Absent &record : m_absent)
        note({record.item});
    return LauncherItem::kFolderPrefix + std::to_string(highest + 1);
}

std::optional<LauncherItem> LauncherModel::takeApplication(const std::string &id)
{
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it->id == id) {
            if (it->isFolder())
                return std::nullopt;
            LauncherItem item = std::move(*it);
            m_items.erase(it);
            return item;
        }
        auto &children = it->children;
        const auto child = std::find_if(children.begin(), children.end(),
                                        [&id](const LauncherItem &item) { return item.id == id; });
        if (child == children.end())
            continue;
        LauncherItem item = std::move(*child);
        children.erase(child);
        // A folder left with one application gives way to it.
        if (children.empty())
            m_items.erase(it);
        else if (children.size() == 1)
            *it = LauncherItem(std::move(children.front()));
        return item;
    }
    return std::nullopt;
}

bool LauncherModel::placeAbsent(std::vector<LauncherItem> *items, const std::vector<Absent> &absent,
                                const Absent &record)
{
    std::vector<LauncherItem> *list = items;
    if (!record.folder.empty()) {
        const auto folder = std::find_if(items->begin(), items->end(),
                                         [&record](const LauncherItem &item) { return item.id == record.folder; });
        if (folder == items->end())
            return false;
        list = &folder->children;
    }

    size_t index = list->size();
    std::string after = record.after;
    std::set<std::string> visited;
    while (true) {
        if (after.empty()) {
            index = 0;
            break;
        }
        if (!visited.insert(after).second)
            break;
        const auto found = std::find_if(list->begin(), list->end(),
                                        [&after](const LauncherItem &item) { return item.id == after; });
        if (found != list->end()) {
            index = size_t(found - list->begin()) + 1;
            break;
        }
        const auto before = std::find_if(absent.begin(), absent.end(), [&record, &after](const Absent &other) {
            return other.folder == record.folder && other.item.id == after;
        });
        if (before == absent.end())
            break;
        after = before->after;
    }
    list->insert(list->begin() + std::ptrdiff_t(index), record.item);
    return true;
}

std::vector<LauncherItem> LauncherModel::itemsWithAbsent() const
{
    std::vector<LauncherItem> items = m_items;
    for (const Absent &record : m_absent) {
        if (record.folder.empty())
            placeAbsent(&items, m_absent, record);
    }
    for (const Absent &record : m_absent) {
        if (!record.folder.empty() && !placeAbsent(&items, m_absent, record))
            items.push_back(record.item);
    }
    return items;
}

void LauncherModel::changed()
{
    // A folder is new while one of its applications is.
    for (LauncherItem &item : m_items) {
        if (item.isFolder())
            item.newInstalled = std::any_of(item.children.begin(), item.children.end(),
                                            [](const LauncherItem &child) { return child.newInstalled; });
    }
    updateRows();
    delaySave();
}

std::string LauncherModel::createFolder(const std::string &id, const std::string &name)
{
    const int index = findById(id);
    if (!m_searchKey.empty() || index < 0 || m_items[size_t(index)].isFolder())
        return {};
    LauncherItem folder;
    folder.id = newFolderId();
    folder.name = name;
    folder.children.push_back(std::move(m_items[size_t(index)]));
    m_items[size_t(index)] = std::move(folder);
    changed();
    return m_items[size_t(index)].id;
}

std::string LauncherModel::merge(const std::string &id, const std::string &target, const std::string &name)
{
    if (!m_searchKey.empty() || id == target || LauncherItem::isFolderId(id) || findById(target) < 0
        || folderOf(id) == target)
        return {};
    std::optional<LauncherItem> app = takeApplication(id);
    if (!app)
        return {};
    // The folder the application left, if any, is not the target: the
    // target stays, though its index may change.
    const int index = findById(target);
    LauncherItem &item = m_items[size_t(index)];
    if (!item.isFolder()) {
        LauncherItem folder;
        folder.id = newFolderId();
        folder.name = name;
        folder.children.push_back(std::move(item));
        item = std::move(folder);
    }
    item.children.push_back(std::move(*app));
    const std::string folder = item.id;
    changed();
    return folder;
}

void LauncherModel::removeFromFolder(const std::string &id)
{
    const std::string folder = folderOf(id);
    if (!m_searchKey.empty() || folder.empty())
        return;
    const int index = findById(folder);
    const size_t count = m_items.size();
    std::optional<LauncherItem> app = takeApplication(id);
    // Just after the folder, or after the application that took its place;
    // in its place when nothing did.
    const bool slotStays = m_items.size() == count;
    m_items.insert(m_items.begin() + index + (slotStays ? 1 : 0), std::move(*app));
    changed();
}

void LauncherModel::moveInFolder(const std::string &folder, int from, int to)
{
    const int index = findById(folder);
    if (!m_searchKey.empty() || index < 0 || from == to)
        return;
    auto &children = m_items[size_t(index)].children;
    if (from < 0 || to < 0 || size_t(from) >= children.size() || size_t(to) >= children.size())
        return;
    LauncherItem item = std::move(children[size_t(from)]);
    children.erase(children.begin() + from);
    children.insert(children.begin() + to, std::move(item));
    changed();
}

void LauncherModel::renameFolder(const std::string &folder, const std::string &name)
{
    const int index = findById(folder);
    if (index < 0 || !m_items[size_t(index)].isFolder() || m_items[size_t(index)].name == name)
        return;
    m_items[size_t(index)].name = name;
    changed();
}

void LauncherModel::ungroupFolder(const std::string &folder)
{
    const int index = findById(folder);
    if (!m_searchKey.empty() || index < 0 || !m_items[size_t(index)].isFolder())
        return;
    std::vector<LauncherItem> children = std::move(m_items[size_t(index)].children);
    m_items.erase(m_items.begin() + index);
    m_items.insert(m_items.begin() + index, std::make_move_iterator(children.begin()),
                   std::make_move_iterator(children.end()));
    changed();
}

void LauncherModel::sendToDesktop(const std::string &key)
{
    DesktopEntryPtr entry = m_applications.byId(key);
    if (!entry)
        entry = m_applications.byPath(key);
    if (!entry)
        return;
    // A link, not a copy: the file manager runs only launchers that resolve
    // into a menu directory.
    fileThread().post([source = entry->path] {
        const std::string desktop = base::paths::userDirectory(base::paths::UserDirectory::Desktop);
        const std::string target = desktop + '/' + std::filesystem::path(source).filename().string();
        (void)symlink(source.c_str(), target.c_str());
    });
}

bool LauncherModel::launchRow(int row)
{
    if (row < 0 || row >= count())
        return false;
    return launch(rows()[size_t(row)].id);
}

bool LauncherModel::launch(const std::string &idOrPath)
{
    DesktopEntryPtr entry = m_applications.byId(idOrPath);
    if (!entry)
        entry = m_applications.byPath(idOrPath);
    if (!entry)
        return false;

    clearNewInstalled({entry->id});
    const std::vector<std::string> command = entry->command();
    if (command.empty())
        return false;
    m_launcher.launchCommand(command, entry->workingDirectory, entry->id);
    applicationLaunched();
    return true;
}

void LauncherModel::onApplicationsChanged()
{
    if (!reconcile())
        return;
    updateRows();
    delaySave();
}

bool LauncherModel::reconcile()
{
    if (!m_loaded || m_applications.isLoading())
        return false;

    // Only entries that pass the display rules: a saved list must not bring
    // back an application that is now hidden or not for this desktop.
    const std::vector<DesktopEntryPtr> &available = m_applications.applications();
    std::map<std::string, DesktopEntryPtr> byId;
    std::map<std::string, DesktopEntryPtr> byPath;
    for (const DesktopEntryPtr &entry : available) {
        byId.emplace(entry->id, entry);
        byPath.emplace(absolutePath(entry->path), entry);
    }

    std::vector<LauncherItem> updated;
    std::set<std::string> seen;
    const auto find = [&byId, &byPath](const std::string &id) -> DesktopEntryPtr {
        if (const auto it = byId.find(id); it != byId.end())
            return it->second;
        if (const auto path = byPath.find(absolutePath(id)); path != byPath.end())
            return path->second;
        return nullptr;
    };
    // The application still there, brought up to date; false when it went.
    const auto refresh = [&find, &seen](LauncherItem *item) {
        const DesktopEntryPtr entry = find(item->id);
        if (!entry || entry->terminal || seen.count(entry->id))
            return false;
        updateItem(item, *entry);
        seen.insert(entry->id);
        return true;
    };
    // Gone, but shown this session: an id read at login has no name yet,
    // and one not found then goes for good.
    const auto goneForNow = [&find](const LauncherItem &item) { return !item.name.empty() && !find(item.id); };
    // Through the absent ones too, so each records the item it followed.
    std::set<std::string> absentIds;
    for (const Absent &record : m_absent)
        absentIds.insert(record.item.id);
    std::string previous;
    for (LauncherItem item : itemsWithAbsent()) {
        const std::string after = std::exchange(previous, item.id);
        if (absentIds.count(item.id))
            continue;
        if (!item.isFolder()) {
            if (refresh(&item))
                updated.push_back(item);
            else if (goneForNow(item))
                m_absent.push_back({item, {}, after});
            continue;
        }
        std::vector<LauncherItem> children;
        std::string previousChild;
        for (LauncherItem child : item.children) {
            const std::string childAfter = std::exchange(previousChild, child.id);
            if (absentIds.count(child.id))
                continue;
            if (refresh(&child))
                children.push_back(std::move(child));
            else if (goneForNow(child))
                m_absent.push_back({child, item.id, childAfter});
        }
        // A folder keeps its applications still there, and goes with the
        // last; one that loses all but one gives way to it. While one of
        // its applications may come back, it stays, hidden when empty.
        const bool waiting = std::any_of(m_absent.begin(), m_absent.end(),
                                         [&item](const Absent &record) { return record.folder == item.id; });
        if (children.empty()) {
            if (waiting) {
                item.children.clear();
                m_absent.push_back({item, {}, after});
            }
            continue;
        }
        if (children.size() == 1 && item.children.size() > 1 && !waiting) {
            updated.push_back(std::move(children.front()));
            continue;
        }
        item.children = std::move(children);
        updated.push_back(std::move(item));
    }

    // Applications back in their places, a hidden folder with its first.
    std::vector<Absent> returning;
    std::vector<Absent> remaining;
    for (const Absent &record : m_absent) {
        const DesktopEntryPtr entry = record.item.isFolder() ? nullptr : find(record.item.id);
        if (!entry) {
            remaining.push_back(record);
            continue;
        }
        if (entry->terminal || seen.count(entry->id))
            continue;
        Absent back = record;
        updateItem(&back.item, *entry);
        seen.insert(entry->id);
        returning.push_back(std::move(back));
    }
    for (const Absent &record : returning) {
        const auto folderShown = std::any_of(updated.begin(), updated.end(),
                                             [&record](const LauncherItem &item) { return item.id == record.folder; });
        if (!record.folder.empty() && !folderShown) {
            const auto folder = std::find_if(remaining.begin(), remaining.end(), [&record](const Absent &other) {
                return other.folder.empty() && other.item.id == record.folder;
            });
            if (folder != remaining.end()) {
                placeAbsent(&updated, m_absent, *folder);
                remaining.erase(folder);
            }
        }
        if (!placeAbsent(&updated, m_absent, record))
            updated.push_back(record.item);
    }
    // A hidden folder waits only for its applications.
    std::set<std::string> awaited;
    for (const Absent &record : remaining)
        awaited.insert(record.folder);
    std::erase_if(remaining,
                  [&awaited](const Absent &record) { return record.item.isFolder() && !awaited.count(record.item.id); });
    m_absent = std::move(remaining);

    for (LauncherItem &item : updated) {
        if (item.isFolder())
            item.newInstalled = std::any_of(item.children.begin(), item.children.end(),
                                            [](const LauncherItem &child) { return child.newInstalled; });
    }

    for (const DesktopEntryPtr &entry : available) {
        if (entry->terminal || seen.count(entry->id))
            continue;
        LauncherItem item;
        updateItem(&item, *entry);
        item.newInstalled = !m_firstLoad && !m_openedUnknownIds.erase(entry->id);
        updated.push_back(item);
        seen.insert(entry->id);
    }

    if (m_firstLoad) {
        const base::Locale &locale = base::Locale::system();
        std::sort(updated.begin(), updated.end(), [&locale](const LauncherItem &a, const LauncherItem &b) {
            return locale.compare(a.name, b.name) < 0;
        });
        m_firstLoad = false;
    }

    m_items = std::move(updated);
    return true;
}

LauncherPageModel::LauncherPageModel(LauncherModel &source)
    : m_source(source)
    , m_rows(*this, [](const std::string &id) { return id; })
{
    const auto resync = [this](int, int) { sync(); };
    m_subscriptions.push_back(m_source.rowsInserted.connect(resync));
    m_subscriptions.push_back(m_source.rowsRemoved.connect(resync));
    m_subscriptions.push_back(m_source.rowMoved.connect(resync));
    m_subscriptions.push_back(m_source.modelReset.connect([this] { this->sync(); }));
    m_subscriptions.push_back(m_source.dataChanged.connect([this](int first, int last) {
        for (int row = 0; row < count(); ++row) {
            const int source = sourceRow(row);
            if (source >= first && source <= last)
                dataChanged(row, row);
        }
    }));
    sync();
}

void LauncherPageModel::setStartIndex(int startIndex)
{
    if (startIndex == m_startIndex)
        return;
    m_startIndex = startIndex;
    sync();
}

void LauncherPageModel::setLimitCount(int limitCount)
{
    if (limitCount == m_limitCount)
        return;
    m_limitCount = limitCount;
    sync();
}

int LauncherPageModel::sourceRow(int row) const
{
    if (row < 0 || row >= count())
        return -1;
    const std::string &id = m_rows.rows()[size_t(row)];
    const auto &rows = m_source.rows();
    const int end = std::min(int(rows.size()), m_startIndex + m_limitCount);
    for (int source = std::max(0, m_startIndex); source < end; ++source) {
        if (rows[size_t(source)].id == id)
            return source;
    }
    return -1;
}

const LauncherItem *LauncherPageModel::item(int row) const
{
    const int source = sourceRow(row);
    return source < 0 ? nullptr : &m_source.rows()[size_t(source)];
}

void LauncherPageModel::sync()
{
    std::vector<std::string> ids;
    const auto &rows = m_source.rows();
    const int end = std::min(int(rows.size()), m_startIndex + m_limitCount);
    for (int source = std::max(0, m_startIndex); source < end; ++source)
        ids.push_back(rows[size_t(source)].id);
    m_rows.sync(ids);
}

} // namespace cutefish::shell
