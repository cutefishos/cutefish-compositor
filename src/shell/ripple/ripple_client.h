// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_RIPPLE_RIPPLE_CLIENT_H_
#define CUTEFISH_SHELL_RIPPLE_RIPPLE_CLIENT_H_

#include <optional>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"

namespace cutefish::shell {

struct RipplePeer
{
    std::string alias;
    // "mobile", "desktop", "web", "headless" or "server".
    std::string type;
    std::string fingerprint;
    std::string host;
};

struct RippleOfferFile
{
    std::string id;
    std::string name;
    unsigned long long size = 0;
};

struct RippleOffer
{
    std::string session;
    std::string sender;
    std::vector<RippleOfferFile> files;
};

struct RippleTransfer
{
    std::string session;
    // "send" or "receive".
    std::string direction;
    std::string file;
    unsigned long long done = 0;
    unsigned long long total = 0;
};

// The shell's side of org.cutefish.Ripple: the devices it found, the offers it
// holds, and the transfers it runs. The daemon does the talking; this only
// asks and listens.
class RippleClient
{
public:
    explicit RippleClient(base::EventLoop *loop);
    ~RippleClient();

    RippleClient(const RippleClient &) = delete;
    RippleClient &operator=(const RippleClient &) = delete;

    // False when the daemon is not running.
    bool available() const;

    std::vector<RipplePeer> peers() const;
    // Asks the others on the network to answer now.
    void announce();
    // Sends the files to the device with that fingerprint; the session the
    // daemon started, empty when it refused.
    std::string send(const std::vector<std::string> &paths, const std::string &fingerprint);
    // accepts empty declines the whole offer.
    void respond(const std::string &session, const std::vector<std::string> &accepted);
    void cancel(const std::string &session);
    // While up, the daemon leaves received files to the panel instead of a
    // notification, as AirDrop's window does.
    void setPanelVisible(bool visible);
    // The panel is closing with the offer unanswered; let a notification ask.
    void notifyOffer(const std::string &session);

    base::Signal<> peersChanged;
    base::Signal<RippleOffer> incoming;
    base::Signal<RippleTransfer> progress;
    // session, whether it ended well, why not, and where a received file went.
    base::Signal<const std::string &, bool, const std::string &, const std::string &> finished;

private:
    base::dbus::Proxy m_proxy;
    std::vector<base::dbus::Match> m_matches;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_RIPPLE_RIPPLE_CLIENT_H_
