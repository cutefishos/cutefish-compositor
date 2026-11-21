// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/common/applications.h"

#include <algorithm>

#include "base/application_registry.h"
#include "base/locale.h"
#include "ui/icon.h"

namespace cutefish::shell {

RegistryApplications::RegistryApplications()
    : m_registry(base::ApplicationRegistry::instance())
{
    update();
    m_changed = m_registry.changed.connect([this] {
        // A new application's icons arrive with its desktop entry.
        ui::reloadThemeIcons();
        update();
        changed();
    });
}

void RegistryApplications::update()
{
    m_applications = m_registry.applications();

    // An installed bundle has no command while nothing runs bundles, yet it
    // stays listed: opening it offers to install what runs it.
    const base::Locale &locale = base::Locale::system();
    for (const DesktopEntryPtr &entry : m_registry.entries()) {
        if (!entry->bundle || !entry->exec.empty() || entry->hidden || entry->noDisplay)
            continue;
        const auto position = std::upper_bound(m_applications.begin(), m_applications.end(), entry,
                                               [&locale](const DesktopEntryPtr &a, const DesktopEntryPtr &b) {
                                                   return locale.compare(a->name, b->name) < 0;
                                               });
        m_applications.insert(position, entry);
    }
}

bool RegistryApplications::isLoading() const
{
    return m_registry.isLoading();
}

DesktopEntryPtr RegistryApplications::byId(const std::string &id) const
{
    return id.empty() ? nullptr : m_registry.byId(id);
}

DesktopEntryPtr RegistryApplications::byPath(const std::string &path) const
{
    return path.empty() ? nullptr : m_registry.byPath(path);
}

DesktopEntryPtr RegistryApplications::resolveWindow(const std::string &appId, const std::string &windowClass,
                                                    const std::string &executablePath,
                                                    const std::string &executableName) const
{
    return m_registry.resolveWindow(appId, windowClass, executablePath, executableName);
}

const std::vector<DesktopEntryPtr> &RegistryApplications::applications() const
{
    return m_applications;
}

} // namespace cutefish::shell
