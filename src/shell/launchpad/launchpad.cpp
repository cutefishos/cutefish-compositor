// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/launchpad/launchpad.h"

#include <algorithm>

#include "base/logging.h"
#include "shell/common/task_manager.h"
#include "shell/dock/dock.h"

namespace cutefish::shell {

static const base::LogCategory cfLaunchpad("cutefish.shell.launchpad");

Launchpad::Launchpad(base::dbus::Connection *bus, TaskManager &tasks, ApplicationLauncher &launcher, Dock *dock,
                     std::string settingsPath)
    : m_bus(bus)
    , m_tasks(tasks)
    , m_dock(dock)
    , m_applications(tasks.applicationSource(), launcher, std::move(settingsPath))
{
    const auto note = [this](const std::string &application) { noteApplicationOpened(application); };
    m_subscriptions.push_back(m_tasks.applicationAdded.connect(note));
    // A window can open before the registry has its new entry.
    m_subscriptions.push_back(m_tasks.applicationChanged.connect(note));
    const auto noteRunning = [this] {
        for (const std::string &application : std::vector<std::string>(m_tasks.applications()))
            noteApplicationOpened(application);
    };
    m_subscriptions.push_back(m_tasks.readyChanged.connect(noteRunning));
    if (m_tasks.isReady())
        noteRunning();

    // Opened applications lose their new mark.
    m_subscriptions.push_back(applicationOpened.connect(
            [this](const std::string &desktopId) { m_applications.clearNewInstalled({desktopId}); }));
}

Launchpad::~Launchpad()
{
    if (m_object && m_bus)
        m_bus->releaseName("org.cutefish.Launchpad");
}

bool Launchpad::registerService()
{
    if (m_object)
        return true;
    if (!m_bus)
        return false;
    auto object = std::make_unique<base::dbus::Object>(m_bus, "/Launchpad", "org.cutefish.Launchpad");
    object->addMethod("show", [this] { show(); });
    object->addMethod("hide", [this] { hide(); });
    object->addMethod("toggle", [this] { toggle(); });
    if (!object->publish()
        || !m_bus->requestName("org.cutefish.Launchpad", base::dbus::Connection::DoNotQueue)) {
        CF_WARNING(cfLaunchpad) << "Cannot own org.cutefish.Launchpad";
        return false;
    }
    m_object = std::move(object);
    return true;
}

void Launchpad::show()
{
    m_shown = true;
    if (showWindow)
        showWindow();
}

void Launchpad::hide()
{
    m_shown = false;
    setDragging(false);
    if (hideWindow)
        hideWindow();
}

void Launchpad::toggle()
{
    m_shown ? hide() : show();
}

void Launchpad::deactivated()
{
    if (m_shown && !m_dragging)
        hide();
}

bool Launchpad::isPinnedInDock(const std::string &desktop) const
{
    return m_dock && m_dock->applications().isDesktopPinned(desktop);
}

void Launchpad::sendToDock(const std::string &desktop)
{
    if (m_dock)
        m_dock->applications().addItem(desktop);
}

void Launchpad::removeFromDock(const std::string &desktop)
{
    if (m_dock)
        m_dock->applications().removeItem(desktop);
}

void Launchpad::noteApplicationOpened(const std::string &application)
{
    const DesktopEntryPtr entry = m_tasks.entry(application);
    if (!entry)
        return;
    const std::string &desktopId = entry->id;
    if (!m_openedTaken
        && std::find(m_openedApplications.begin(), m_openedApplications.end(), desktopId)
                == m_openedApplications.end())
        m_openedApplications.push_back(desktopId);
    applicationOpened(desktopId);
}

std::vector<std::string> Launchpad::takeOpenedApplications()
{
    m_openedTaken = true;
    return std::exchange(m_openedApplications, {});
}

void Launchpad::setDragging(bool dragging)
{
    m_dragging = dragging;
}

} // namespace cutefish::shell
