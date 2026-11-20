// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_GTK_SETTINGS_H_
#define CUTEFISH_SHELL_MENUBAR_GTK_SETTINGS_H_

#include <memory>

#include "base/dbus.h"

namespace cutefish::shell {

// org.gtk.Settings at /org/gtk/Settings, where GTK 3 on Wayland reads the
// modules it loads.
class GtkSettings
{
public:
    static constexpr const char *kModules = "appmenu-gtk-module";

    explicit GtkSettings(base::dbus::Connection *bus);
    ~GtkSettings();

    GtkSettings(const GtkSettings &) = delete;
    GtkSettings &operator=(const GtkSettings &) = delete;

    bool registerService();

private:
    base::dbus::Connection *m_bus;
    std::unique_ptr<base::dbus::Object> m_object;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_GTK_SETTINGS_H_
