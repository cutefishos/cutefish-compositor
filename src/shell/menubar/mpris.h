// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MPRIS_H_
#define CUTEFISH_SHELL_MENUBAR_MPRIS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "shell/common/pending_calls.h"
#include "shell/common/sender_filter.h"

namespace cutefish::shell {

class MprisPlayer
{
public:
    enum class PlaybackStatus {
        Stopped,
        Playing,
        Paused,
    };

    MprisPlayer(base::dbus::Connection *bus, std::string service);
    ~MprisPlayer();

    MprisPlayer(const MprisPlayer &) = delete;
    MprisPlayer &operator=(const MprisPlayer &) = delete;

    const std::string &service() const { return m_service; }
    bool isReady() const { return m_rootReady && m_playerReady; }

    std::string identity() const;

    PlaybackStatus playbackStatus() const;
    bool canControl() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    std::string title() const;
    std::string artist() const;
    std::string artUrl() const;

    void play();
    void pause();
    void playPause();
    void stop();
    void next();
    void previous();

    base::Signal<> changed;
    base::Signal<> playbackStatusChanged;

private:
    void load(const std::string &interface);
    void apply(const std::string &interface, const base::dbus::VariantMap &properties);
    bool playerFlag(const char *name, bool fallback = false) const;
    void callPlayer(const char *method);

    base::dbus::Connection *m_bus;
    std::string m_service;
    base::dbus::Proxy m_root;
    base::dbus::Proxy m_player;
    SenderFilter m_sender;
    std::vector<base::dbus::Match> m_matches;
    PendingCalls m_calls;
    base::dbus::VariantMap m_rootProperties;
    base::dbus::VariantMap m_playerProperties;
    base::dbus::VariantMap m_metadata;
    bool m_rootReady = false;
    bool m_playerReady = false;
};

class MprisManager
{
public:
    explicit MprisManager(base::dbus::Connection *bus);
    ~MprisManager();

    MprisManager(const MprisManager &) = delete;
    MprisManager &operator=(const MprisManager &) = delete;

    MprisPlayer *current() const { return m_current.get(); }
    std::string currentService() const;
    std::vector<std::string> availableServices() const;

    base::Signal<> currentChanged;
    base::Signal<> currentPlayerChanged;

private:
    struct Entry
    {
        std::shared_ptr<MprisPlayer> player;
        uint64_t active = 0;
        base::Subscription statusChanged;
    };

    void add(const std::string &service);
    void remove(const std::string &service);
    void statusChanged(const std::string &service);
    Entry *entry(const std::string &service);
    bool present(const std::shared_ptr<MprisPlayer> &player) const;
    std::shared_ptr<MprisPlayer> choose() const;
    void show(std::shared_ptr<MprisPlayer> player);

    base::dbus::Connection *m_bus;
    std::vector<Entry> m_entries;
    std::shared_ptr<MprisPlayer> m_current;
    uint64_t m_clock = 0;
    base::Subscription m_currentSubscription;
    base::dbus::Match m_nameOwnerChanged;
    PendingCalls m_calls;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MPRIS_H_
