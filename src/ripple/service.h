// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RIPPLE_SERVICE_H_
#define CUTEFISH_RIPPLE_SERVICE_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "base/dbus.h"
#include "base/event_loop.h"
#include "base/timer.h"
#include "ripple/client.h"
#include "ripple/config.h"
#include "ripple/discovery.h"
#include "ripple/http.h"
#include "ripple/protocol.h"
#include "ripple/tls.h"

namespace cutefish::ripple {

// The daemon's whole state: what the port answers, what the multicast socket
// hears, and the transfers in flight. The shell talks to it over D-Bus; the
// code here never touches the toolkit's windows.
class Service
{
public:
    explicit Service(base::EventLoop &loop);
    ~Service();

    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;

    bool start();
    void stop();

    // The identity this device announces.
    Device self() const;

private:
    struct Incoming
    {
        std::string session;
        Device sender;
        std::vector<FileInfo> files;
        std::map<std::string, std::string> tokens;
        std::map<std::string, std::string> paths;
        std::set<std::string> accepted;
        std::mutex mutex;
        std::condition_variable answered;
        bool decided = false;
        bool acceptedAll = false;
        std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
        uint64_t done = 0;
        uint64_t total = 0;
    };

    struct Outgoing
    {
        std::string session;
        Device peer;
        std::atomic_bool cancelled {false};
    };

    // The HTTP routes and the D-Bus members.
    HttpResponse handleRequest(HttpRequest &request, Stream &body);
    HttpResponse prepareUpload(HttpRequest &request, Stream &body);
    HttpResponse upload(HttpRequest &request, Stream &body);
    void registerService();
    void announceNow();
    void emitPeers();
    void emitProgress(const TransferUpdate &update);
    void emitFinished(const std::string &session, bool ok, const std::string &error,
                      const std::string &folder = {});

    std::string statusJson() const;
    std::string peersJson() const;
    std::string send(const std::string &pathsJson, const std::string &fingerprint);
    void cancel(const std::string &session);
    void respond(const std::string &session, const std::string &acceptedJson);

    void sendWorker(std::shared_ptr<Outgoing> outgoing, std::vector<std::string> paths);
    // The notification that says what arrived, with a way to the folder.
    void notifyReceived(const std::string &sender, const std::vector<std::string> &paths);
    // The offer a closed panel left behind, answerable from the notification.
    void notifyOffer(const std::string &session);
    void pruneSessions();

    std::string choosePath(const Device &sender, const std::string &name) const;

    base::EventLoop &m_loop;
    base::LoopHandle m_handle;
    Config m_config;
    Certificate m_certificate;
    Discovery m_discovery;
    Server m_server;
    Client m_client;
    base::dbus::Connection *m_bus = nullptr;
    std::unique_ptr<base::dbus::Object> m_object;
    std::unique_ptr<base::dbus::Proxy> m_notifications;
    base::dbus::Match m_actionInvoked;
    base::dbus::Match m_notificationClosed;
    // Notification id: the folder to open and the file to open.
    std::map<uint32_t, std::pair<std::string, std::string>> m_notified;
    // Notification id: the offer it answers, and the files to accept.
    std::map<uint32_t, std::pair<std::string, std::vector<std::string>>> m_offerNotifications;
    // The panel shows what arrived; no notification while it is up, as
    // AirDrop shows its own window instead of Notification Center.
    bool m_panelVisible = false;
    base::Timer m_announce;
    base::Timer m_prune;
    int m_fdWatch = 0;

    mutable std::mutex m_sessionsMutex;
    std::map<std::string, std::shared_ptr<Incoming>> m_incoming;
    std::map<std::string, std::shared_ptr<Outgoing>> m_outgoing;
    // Sending threads outlive their session entry, so they are joined here.
    std::vector<std::thread> m_workers;
};

} // namespace cutefish::ripple

#endif // CUTEFISH_RIPPLE_SERVICE_H_
