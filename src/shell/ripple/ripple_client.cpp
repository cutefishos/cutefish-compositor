// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/ripple/ripple_client.h"

#include "base/json.h"

namespace cutefish::shell {

namespace {

std::vector<RipplePeer> parsePeers(const std::string &text)
{
    std::vector<RipplePeer> peers;
    const auto parsed = base::json::Value::parse(text);
    if (!parsed || !parsed->isArray())
        return peers;
    for (const base::json::Value &value : parsed->toArray()) {
        if (!value.isObject())
            continue;
        RipplePeer peer;
        peer.alias = value["alias"].toString();
        peer.type = value["deviceType"].toString("desktop");
        peer.fingerprint = value["fingerprint"].toString();
        peer.host = value["host"].toString();
        if (!peer.fingerprint.empty())
            peers.push_back(std::move(peer));
    }
    return peers;
}

std::string forSend(const std::vector<std::string> &paths)
{
    base::json::Value::Array array;
    for (const std::string &path : paths)
        array.push_back(base::json::Value(path));
    return base::json::Value(std::move(array)).serialize();
}

} // namespace

RippleClient::RippleClient(base::EventLoop *loop)
    : m_proxy(base::dbus::Connection::session(loop), "org.cutefish.Ripple", "/Ripple",
              "org.cutefish.Ripple")
{
    m_matches.push_back(m_proxy.onSignal("peersChanged", [this](const base::dbus::Message &) {
        peersChanged();
    }));
    m_matches.push_back(m_proxy.onSignal("incoming", [this](const base::dbus::Message &message) {
        const auto text = message.argument<std::string>(0);
        if (!text)
            return;
        const auto parsed = base::json::Value::parse(*text);
        if (!parsed || !parsed->isObject())
            return;
        RippleOffer offer;
        offer.session = (*parsed)["session"].toString();
        offer.sender = (*parsed)["sender"]["alias"].toString();
        const base::json::Value &files = (*parsed)["files"];
        if (files.isArray()) {
            for (const base::json::Value &value : files.toArray()) {
                RippleOfferFile file;
                file.id = value["id"].toString();
                file.name = value["name"].toString();
                file.size = static_cast<unsigned long long>(value["size"].toInt(0));
                offer.files.push_back(std::move(file));
            }
        }
        if (!offer.session.empty())
            incoming(std::move(offer));
    }));
    m_matches.push_back(m_proxy.onSignal("progress", [this](const base::dbus::Message &message) {
        const auto text = message.argument<std::string>(0);
        if (!text)
            return;
        const auto parsed = base::json::Value::parse(*text);
        if (!parsed || !parsed->isObject())
            return;
        RippleTransfer transfer;
        transfer.session = (*parsed)["session"].toString();
        transfer.direction = (*parsed)["direction"].toString();
        transfer.file = (*parsed)["file"].toString();
        transfer.done = static_cast<unsigned long long>((*parsed)["done"].toInt(0));
        transfer.total = static_cast<unsigned long long>((*parsed)["total"].toInt(0));
        progress(std::move(transfer));
    }));
    m_matches.push_back(m_proxy.onSignal("finished", [this](const base::dbus::Message &message) {
        const auto text = message.argument<std::string>(0);
        if (!text)
            return;
        const auto parsed = base::json::Value::parse(*text);
        if (!parsed || !parsed->isObject())
            return;
        const std::string session = (*parsed)["session"].toString();
        const bool ok = (*parsed)["ok"].toBool();
        const std::string error = (*parsed)["error"].toString();
        const std::string folder = (*parsed)["folder"].toString();
        finished(session, ok, error, folder);
    }));
}

RippleClient::~RippleClient() = default;

bool RippleClient::available() const
{
    return m_proxy.call("status").value<std::string>().has_value();
}

void RippleClient::announce()
{
    m_proxy.call("announce");
}

std::vector<RipplePeer> RippleClient::peers() const
{
    const auto text = m_proxy.call("peers").value<std::string>();
    return text ? parsePeers(*text) : std::vector<RipplePeer>();
}

std::string RippleClient::send(const std::vector<std::string> &paths, const std::string &fingerprint)
{
    if (paths.empty() || fingerprint.empty())
        return {};
    const auto session = m_proxy.call("send", forSend(paths), fingerprint).value<std::string>();
    return session.value_or(std::string());
}

void RippleClient::setPanelVisible(bool visible)
{
    m_proxy.send("panelVisible", visible);
}

void RippleClient::notifyOffer(const std::string &session)
{
    if (!session.empty())
        m_proxy.send("notifyOffer", session);
}

void RippleClient::respond(const std::string &session, const std::vector<std::string> &accepted)
{
    base::json::Value::Array array;
    for (const std::string &id : accepted)
        array.push_back(base::json::Value(id));
    m_proxy.call("respond", session, base::json::Value(std::move(array)).serialize());
}

void RippleClient::cancel(const std::string &session)
{
    if (!session.empty())
        m_proxy.call("cancel", session);
}

} // namespace cutefish::shell
