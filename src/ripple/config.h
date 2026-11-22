// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RIPPLE_CONFIG_H_
#define CUTEFISH_RIPPLE_CONFIG_H_

#include <string>

#include "base/settings.h"

namespace cutefish::ripple {

// What Settings ▸ Ripple writes, and what the daemon reads when it starts.
class Config
{
public:
    Config();

    // The name other devices see; the computer's name when it was never set.
    const std::string &name() const { return m_name; }
    int port() const { return m_port; }
    // Whether an announcement goes out and the port listens.
    bool visible() const { return m_visible; }
    // Whether an incoming offer waits for the user, or lands at once.
    bool askBeforeReceiving() const { return m_ask; }
    // Where received files go, under a folder named after the sender.
    const std::string &downloads() const { return m_downloads; }

    void setName(const std::string &name);
    void setPort(int port);
    void setVisible(bool visible);
    void setAskBeforeReceiving(bool ask);
    void setDownloads(const std::string &path);
    // Reads the file again, after Settings wrote it.
    void reload();

private:
    void save();

    base::Settings m_settings;
    std::string m_name;
    int m_port = 53317;
    bool m_visible = true;
    bool m_ask = true;
    std::string m_downloads;
};

// ~/.config/cutefish/ripple.conf
std::string configPath();
// ~/.config/cutefish/ripple, where the certificate and key live.
std::string dataPath();

} // namespace cutefish::ripple

#endif // CUTEFISH_RIPPLE_CONFIG_H_
