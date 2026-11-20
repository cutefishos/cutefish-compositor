// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_WM_DOCK_PREFERENCES_H_
#define CUTEFISH_WM_DOCK_PREFERENCES_H_

#include "base/file_watcher.h"
#include "base/settings.h"

namespace cutefish::wm {

class DockLayoutManager;
class WindowManager;

class DockPreferences
{
public:
    DockPreferences(DockLayoutManager &layout, WindowManager &manager);

    void reload();

private:
    DockLayoutManager &m_layout;
    WindowManager &m_manager;
    base::Settings m_settings;
    base::FileWatch m_watch;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_DOCK_PREFERENCES_H_
