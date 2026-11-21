// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_APPLICATIONS_H_
#define CUTEFISH_SHELL_COMMON_APPLICATIONS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/desktop_entry.h"
#include "base/signal.h"

namespace cutefish::base {
class ApplicationRegistry;
} // namespace cutefish::base

namespace cutefish::shell {

using DesktopEntryPtr = std::shared_ptr<const base::DesktopEntry>;

class Applications
{
public:
    virtual ~Applications() = default;

    virtual bool isLoading() const = 0;
    virtual DesktopEntryPtr byId(const std::string &id) const = 0;
    virtual DesktopEntryPtr byPath(const std::string &path) const = 0;
    // Null when none fits.
    virtual DesktopEntryPtr resolveWindow(const std::string &appId, const std::string &windowClass,
                                          const std::string &executablePath,
                                          const std::string &executableName) const = 0;
    // Sorted by name. Installed bundles are listed while nothing runs them,
    // with no command.
    virtual const std::vector<DesktopEntryPtr> &applications() const = 0;

    base::Signal<> changed;
};

class RegistryApplications : public Applications
{
public:
    RegistryApplications();

    bool isLoading() const override;
    DesktopEntryPtr byId(const std::string &id) const override;
    DesktopEntryPtr byPath(const std::string &path) const override;
    DesktopEntryPtr resolveWindow(const std::string &appId, const std::string &windowClass,
                                  const std::string &executablePath,
                                  const std::string &executableName) const override;
    const std::vector<DesktopEntryPtr> &applications() const override;

private:
    void update();

    base::ApplicationRegistry &m_registry;
    std::vector<DesktopEntryPtr> m_applications;
    base::Subscription m_changed;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_APPLICATIONS_H_
