// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_DOCK_DOCK_MODEL_H_
#define CUTEFISH_SHELL_DOCK_DOCK_MODEL_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/thread_pool.h"
#include "base/timer.h"
#include "shell/common/applications.h"
#include "shell/common/list_model.h"

namespace cutefish::compositor {
class MenuModel;
}

namespace cutefish::shell {

class ApplicationLauncher;
class RunningApplications;
class TaskManager;
class TaskWindows;

// Desktop ids in order, under Dock/Pinned in shell.conf; read and written on
// the file thread.
namespace pinned_applications {

const std::vector<std::string> &defaults();
// Nothing when the file has no list yet.
std::optional<std::vector<std::string>> load(const std::string &path);
void save(const std::string &path, const std::vector<std::string> &desktopIds);

} // namespace pinned_applications

struct DockItem {
    std::string id;
    std::string iconName;
    std::string visibleName;
    std::string desktopPath;
    std::vector<uint64_t> windows;
    bool running = false;
    size_t currentActive = 0;
    bool isActive = false;
    bool isPinned = false;
    bool fixed = false;
    bool uninstalled = false;
    bool missing = false;

    bool pinnable() const { return !fixed && !desktopPath.empty() && !uninstalled; }
};

class DockModel : public ListModelSignals
{
public:
    static constexpr const char *kLaunchpadId = "cutefish-launchpad";
    static constexpr std::chrono::milliseconds kQuitGrace{3000};
    // An upgrade can remove a desktop file and put it back seconds later.
    static constexpr std::chrono::milliseconds kUninstallGrace{3000};

    // Everything must outlive the model; without `running`, applications run
    // while they have windows.
    DockModel(TaskManager &tasks, ApplicationLauncher &launcher, std::string pinsPath,
              RunningApplications *running = nullptr);
    ~DockModel();

    DockModel(const DockModel &) = delete;
    DockModel &operator=(const DockModel &) = delete;

    std::function<void()> hideLaunchpad;
    std::function<void()> toggleLaunchpad;
    std::function<void(const std::string &id)> launched;

    bool isLoaded() const { return m_loaded; }

    const std::vector<std::unique_ptr<DockItem>> &items() const { return m_items; }
    int count() const { return int(m_items.size()); }
    const DockItem &at(int row) const { return *m_items[size_t(row)]; }
    int indexOf(const std::string &id) const;
    int pinnedCount() const;
    int firstMovableRow() const;

    // By desktop path or desktop id.
    void addItem(const std::string &desktopFile);
    void removeItem(const std::string &desktopFile);
    bool isDesktopPinned(const std::string &desktopFile) const;
    // -1 for none.
    int desktopRow(const std::string &desktopFile);

    void clicked(const std::string &id);
    void raiseWindow(const std::string &id);
    void activateWindow(uint64_t window);
    struct WindowInfo {
        uint64_t id;
        std::string title;
        bool active;
    };
    std::vector<WindowInfo> windows(const std::string &id) const;
    std::shared_ptr<compositor::MenuModel> dockMenu(const std::string &id) const;

    bool openNewInstance(const std::string &id);
    // Whether the item's application opens files of every one of the types,
    // by the types its desktop entry lists and their subtypes.
    bool opensFiles(const std::string &id, const std::vector<std::string> &mimeTypes) const;
    // Opens the files with the item's application, as dropping them on its
    // icon does.
    bool openFiles(const std::string &id, const std::vector<std::string> &paths);
    void quit(const std::string &id);
    void pin(const std::string &id);
    void unPin(const std::string &id);

    void move(int from, int to, bool commit = false);
    bool dropApplication(const std::string &id, int row, bool pinned);
    bool pinAt(int from, int row);
    // `row` counts the existing entry still in its old place.
    bool insert(const std::string &desktopFile, int row, int existingRow = -1);

    std::string iconNameForWindow(uint64_t window) const;
    // Empty if none has it yet.
    std::string appIdForWindow(uint64_t window) const;

    void retranslate();

    base::Signal<> sectionsChanged;
    base::Signal<> loaded;

private:
    DockItem *findById(const std::string &id) const;
    DockItem *findByWindow(uint64_t window) const;
    DockItem *findByDesktop(const std::string &desktopFile, DesktopEntryPtr *entry = nullptr) const;
    int rowOf(const DockItem *item) const;
    void updateFromEntry(DockItem *item, const base::DesktopEntry &entry) const;
    void changed(DockItem *item);
    void moveItem(int from, int to);
    void unPinAt(int from, int row);
    bool unhide(DockItem *item);

    void load();
    void loadPins(const std::optional<std::vector<std::string>> &saved);
    void savePins();
    void onApplicationsChanged();
    void checkInstalled();
    void syncApplication(const std::string &application);
    void onActiveChanged(uint64_t window);

    TaskManager &m_tasks;
    TaskWindows &m_windows;
    Applications &m_applications;
    ApplicationLauncher &m_launcher;
    RunningApplications *m_running;
    std::string m_pinsPath;
    std::vector<std::unique_ptr<DockItem>> m_items;
    bool m_loaded = false;
    std::optional<std::optional<std::vector<std::string>>> m_savedPins;
    base::Timer m_installedTimer;
    base::Job m_pinsJob;
    std::vector<std::unique_ptr<base::Timer>> m_quitTimers;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_DOCK_DOCK_MODEL_H_
