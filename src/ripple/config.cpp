// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ripple/config.h"

#include <unistd.h>

#include <filesystem>

#include "base/paths.h"

namespace cutefish::ripple {

namespace {

// The computer's name, as the shell shows it in the status bar.
std::string defaultName()
{
    char host[256] = {};
    if (::gethostname(host, sizeof(host) - 1) == 0 && host[0] != '\0')
        return host;
    return "Cutefish";
}

} // namespace

std::string configPath()
{
    return base::paths::cutefishConfig("ripple");
}

std::string dataPath()
{
    // A directory beside ripple.conf, not another settings file.
    return base::paths::configHome() + "/cutefish/ripple";
}

Config::Config()
    : m_settings(configPath())
{
    m_name = m_settings.stringValue("Device/Name", defaultName());
    m_port = int(m_settings.intValue("Device/Port", 53317));
    if (m_port <= 0 || m_port > 65535)
        m_port = 53317;
    m_visible = m_settings.boolValue("Device/Visible", true);
    m_ask = m_settings.boolValue("Receiving/Ask", true);
    m_downloads = m_settings.stringValue(
            "Receiving/Downloads",
            base::paths::userDirectory(base::paths::UserDirectory::Downloads) + "/Ripple");
}

void Config::setName(const std::string &name)
{
    if (name.empty() || name == m_name)
        return;
    m_name = name;
    save();
}

void Config::setPort(int port)
{
    if (port <= 0 || port > 65535 || port == m_port)
        return;
    m_port = port;
    save();
}

void Config::setVisible(bool visible)
{
    if (visible == m_visible)
        return;
    m_visible = visible;
    save();
}

void Config::setAskBeforeReceiving(bool ask)
{
    if (ask == m_ask)
        return;
    m_ask = ask;
    save();
}

void Config::setDownloads(const std::string &path)
{
    if (path.empty() || path == m_downloads)
        return;
    m_downloads = path;
    save();
}

void Config::reload()
{
    m_settings.reload();
    m_name = m_settings.stringValue("Device/Name", m_name);
    const int port = int(m_settings.intValue("Device/Port", m_port));
    if (port > 0 && port < 65536)
        m_port = port;
    m_visible = m_settings.boolValue("Device/Visible", m_visible);
    m_ask = m_settings.boolValue("Receiving/Ask", m_ask);
    m_downloads = m_settings.stringValue("Receiving/Downloads", m_downloads);
}

void Config::save()
{
    m_settings.setValue("Device/Name", m_name);
    m_settings.setValue("Device/Port", m_port);
    m_settings.setValue("Device/Visible", m_visible);
    m_settings.setValue("Receiving/Ask", m_ask);
    m_settings.setValue("Receiving/Downloads", m_downloads);
    m_settings.sync();
}

} // namespace cutefish::ripple
