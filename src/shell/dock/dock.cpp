// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/dock/dock.h"

#include "base/logging.h"
#include "shell/common/file_thread.h"
#include "shell/common/task_manager.h"

namespace cutefish::shell {

namespace dbus = base::dbus;

static const base::LogCategory cfDock("cutefish.shell.dock");

Dock::Dock(dbus::Connection *bus, TaskManager &tasks, ApplicationLauncher &launcher, std::string settingsPath,
           std::unique_ptr<TrashMonitor> trash)
    : m_bus(bus)
    , m_settings(settingsPath)
    , m_running(bus, tasks)
    , m_applications(tasks, launcher, settingsPath, &m_running)
    , m_minimizedWindows(tasks.taskWindows(), m_applications, m_settings)
    , m_trash(trash ? std::move(trash) : std::make_unique<TrashMonitor>())
{
}

Dock::~Dock()
{
    if (m_object && m_bus)
        m_bus->releaseName("org.cutefish.Dock");
    waitForFileThread();
}

void Dock::notifyPrimaryGeometryChanged()
{
    primaryGeometryChanged();
}

bool Dock::registerService()
{
    if (m_object)
        return true;
    if (!m_bus)
        return false;

    auto object = std::make_unique<dbus::Object>(m_bus, "/Dock", "org.cutefish.Dock");
    object->addMethod("setDirection",
                      [this](int32_t direction) { m_settings.setDirection(DockSettings::Direction(direction)); },
                      dbus::ArgumentNames{{"direction"}, {}});
    object->addMethod("setIconSize", [this](int32_t iconSize) { m_settings.setIconSize(iconSize); },
                      dbus::ArgumentNames{{"iconSize"}, {}});
    object->addMethod("setVisibility",
                      [this](int32_t visibility) { m_settings.setVisibility(DockSettings::Visibility(visibility)); },
                      dbus::ArgumentNames{{"visibility"}, {}});
    object->addMethod("setGlassTint", [this](double tint) { m_settings.setGlassTint(tint); },
                      dbus::ArgumentNames{{"tint"}, {}});
    object->addMethod("setMinimizedArea", [this](bool enabled) { m_settings.setMinimizedArea(enabled); },
                      dbus::ArgumentNames{{"enabled"}, {}});
    if (!object->publish() || !m_bus->requestName("org.cutefish.Dock", dbus::Connection::DoNotQueue)) {
        CF_WARNING(cfDock) << "Cannot own org.cutefish.Dock";
        return false;
    }
    m_object = std::move(object);
    return true;
}

} // namespace cutefish::shell
