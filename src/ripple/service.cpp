// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ripple/service.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>

#include "base/i18n.h"
#include "base/json.h"
#include "base/logging.h"
#include "base/process.h"
#include "base/uuid.h"

namespace cutefish::ripple {

using cutefish::i18n::format;
using cutefish::i18n::tr;
using cutefish::i18n::trn;

namespace {

constexpr auto kService = "org.cutefish.Ripple";
constexpr auto kPath = "/Ripple";
constexpr size_t kMaxPrepareBytes = 4 * 1024 * 1024;
constexpr size_t kMaxRegisterBytes = 64 * 1024;

base::LogCategory cfRipple("cutefish.ripple", base::LogLevel::Info);

std::string sanitizedName(const std::string &text, const std::string &fallback)
{
    std::string result;
    for (const char c : text) {
        const unsigned char value = static_cast<unsigned char>(c);
        if (c == '/' || c == '\\' || value < 0x20 || value == 0x7F)
            result.push_back('_');
        else
            result.push_back(c);
    }
    while (!result.empty() && (result.front() == '.' || result.front() == ' '))
        result.erase(result.begin());
    if (result.empty())
        return fallback;
    if (result.size() > 120)
        result.resize(120);
    return result;
}

std::string idsJson(const std::vector<std::string> &ids)
{
    base::json::Value::Array array;
    for (const std::string &id : ids)
        array.push_back(base::json::Value(id));
    return base::json::Value(std::move(array)).serialize();
}

std::string mimeFor(const std::string &name)
{
    const size_t dot = name.find_last_of('.');
    std::string extension = dot == std::string::npos ? std::string() : name.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (extension == "png") return "image/png";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "gif") return "image/gif";
    if (extension == "webp") return "image/webp";
    if (extension == "svg") return "image/svg+xml";
    if (extension == "pdf") return "application/pdf";
    if (extension == "txt" || extension == "md") return "text/plain";
    if (extension == "mp4") return "video/mp4";
    if (extension == "mov") return "video/quicktime";
    if (extension == "mp3") return "audio/mpeg";
    if (extension == "zip") return "application/zip";
    if (extension == "deb") return "application/vnd.debian.binary-package";
    return "application/octet-stream";
}

} // namespace

Service::Service(base::EventLoop &loop)
    : m_loop(loop)
    , m_handle(loop.handle())
    , m_client()
    , m_announce(&loop)
    , m_prune(&loop)
{
}

Service::~Service()
{
    stop();
}

Device Service::self() const
{
    Device device;
    device.alias = m_config.name();
    device.type = "desktop";
    device.model = "Cutefish";
    device.protocol = "https";
    device.fingerprint = m_certificate.fingerprint;
    device.port = m_config.port();
    device.download = false;
    return device;
}

bool Service::start()
{
    m_certificate = loadOrCreateCertificate();
    if (m_certificate.fingerprint.empty()) {
        CF_WARNING(cfRipple) << "Cannot make the certificate for Ripple";
        return false;
    }
    m_client.setCertificate(m_certificate.certificate, m_certificate.key);

    if (!m_server.start(m_config.port(), m_certificate.certificate, m_certificate.key)) {
        CF_WARNING(cfRipple) << "Cannot listen on port" << m_config.port();
        return false;
    }
    m_server.setHandler([this](HttpRequest &request, Stream &body) {
        return handleRequest(request, body);
    });

    m_discovery.setOnChanged([this] {
        m_handle.post([this] { emitPeers(); });
    });
    m_discovery.setOnAnnounce([this](const Device &device) {
        // The protocol answers an announcement with /register, which tells
        // the other device who we are as well.
        std::thread([this, device] {
            std::string reply, error;
            m_client.postJson(device, "/api/localsend/v2/register",
                              self().toJson().serialize(), reply, error);
        }).detach();
    });

    if (!m_discovery.start(m_config.port(), m_certificate.fingerprint)) {
        CF_WARNING(cfRipple) << "Cannot join the discovery group";
        m_server.stop();
        return false;
    }

    m_bus = base::dbus::Connection::session(&m_loop);
    if (m_bus) {
        m_notifications = std::make_unique<base::dbus::Proxy>(
                m_bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                "org.freedesktop.Notifications");
        m_actionInvoked = m_notifications->onSignal("ActionInvoked",
                                                    [this](const base::dbus::Message &message) {
            uint32_t id = 0;
            std::string action;
            if (!message.read(id, action))
                return;
            CF_INFO(cfRipple) << "notification" << id << "action" << action;
            const auto offer = m_offerNotifications.find(id);
            if (offer != m_offerNotifications.end()) {
                respond(offer->second.first, action == "accept" ? idsJson(offer->second.second)
                                                                : std::string("[]"));
                m_offerNotifications.erase(offer);
                return;
            }
            const auto it = m_notified.find(id);
            if (it == m_notified.end())
                return;
            if (action == "show-in-folder") {
                base::Process::startDetached("xdg-open", {it->second.first});
            } else if (action == "default") {
                base::Process::startDetached("xdg-open", {it->second.second});
            }
            return;
        });
        m_notificationClosed = m_notifications->onSignal("NotificationClosed",
                                                         [this](const base::dbus::Message &message) {
            uint32_t id = 0;
            if (message.read(id)) {
                m_notified.erase(id);
                m_offerNotifications.erase(id);
            }
        });
    }
    registerService();
    if (!m_object) {
        m_server.stop();
        m_discovery.stop();
        return false;
    }

    m_fdWatch = m_loop.watch(m_discovery.fd(), EPOLLIN, [this](uint32_t) { m_discovery.handleRead(); });
    if (m_fdWatch == 0)
        CF_WARNING(cfRipple) << "Cannot watch the discovery socket";

    m_announce.setInterval(std::chrono::seconds(30));
    m_announce.onTimeout = [this] {
        announceNow();
        m_discovery.prune();
    };
    m_announce.start();
    m_prune.setInterval(std::chrono::seconds(60));
    m_prune.onTimeout = [this] { pruneSessions(); };
    m_prune.start();

    announceNow();
    CF_INFO(cfRipple) << "Ready as" << m_config.name() << "on port" << m_config.port();
    return true;
}

void Service::stop()
{
    m_announce.stop();
    m_prune.stop();
    if (m_fdWatch) {
        m_loop.unwatch(m_fdWatch);
        m_fdWatch = 0;
    }
    m_object.reset();
    m_server.stop();
    m_discovery.stop();
    {
        const std::lock_guard lock(m_sessionsMutex);
        for (auto &[id, outgoing] : m_outgoing)
            outgoing->cancelled = true;
    }
    for (std::thread &worker : m_workers) {
        if (worker.joinable())
            worker.join();
    }
    {
        const std::lock_guard lock(m_sessionsMutex);
        m_workers.clear();
        m_outgoing.clear();
        m_incoming.clear();
    }
}

void Service::announceNow()
{
    if (m_config.visible())
        m_discovery.announce(self());
}

void Service::registerService()
{
    auto object = std::make_unique<base::dbus::Object>(m_bus, kPath, kService);
    object->addMethod("peers", [this] { return peersJson(); });
    object->addMethod("announce", [this] { announceNow(); });
    object->addMethod("reload", [this] {
        m_config.reload();
        announceNow();
    });
    object->addMethod("panelVisible", [this](bool visible) { m_panelVisible = visible; },
                      base::dbus::ArgumentNames {{"visible"}, {}});
    object->addMethod("notifyOffer", [this](const std::string &session) { notifyOffer(session); },
                      base::dbus::ArgumentNames {{"session"}, {}});
    object->addMethod("status", [this] { return statusJson(); });
    object->addMethod("send", [this](const std::string &paths, const std::string &fingerprint) {
        return send(paths, fingerprint);
    }, base::dbus::ArgumentNames {{"paths", "fingerprint"}, {}});
    object->addMethod("cancel", [this](const std::string &session) { cancel(session); },
                      base::dbus::ArgumentNames {{"session"}, {}});
    object->addMethod("respond", [this](const std::string &session, const std::string &accepted) {
        respond(session, accepted);
    }, base::dbus::ArgumentNames {{"session", "accepted"}, {}});
    object->addSignal("peersChanged", "");
    object->addSignal("incoming", "s", {"session"});
    object->addSignal("progress", "s", {"update"});
    object->addSignal("finished", "s", {"result"});
    if (!object->publish() || !m_bus->requestName(kService, 4 /* DoNotQueue */)) {
        CF_WARNING(cfRipple) << "Another Ripple is already running";
        return;
    }
    m_object = std::move(object);
}

void Service::emitPeers()
{
    if (m_object)
        m_object->emitSignal("peersChanged");
}

void Service::emitProgress(const TransferUpdate &update)
{
    base::json::Value value = base::json::Value::object();
    value.insert("session", update.session);
    value.insert("direction", update.direction);
    value.insert("file", update.file);
    value.insert("done", int64_t(update.done));
    value.insert("total", int64_t(update.total));
    if (m_object)
        m_object->emitSignal("progress", value.serialize());
}

void Service::emitFinished(const std::string &session, bool ok, const std::string &error,
                           const std::string &folder)
{
    base::json::Value value = base::json::Value::object();
    value.insert("session", session);
    value.insert("ok", ok);
    value.insert("error", error);
    if (!folder.empty())
        value.insert("folder", folder);
    if (m_object)
        m_object->emitSignal("finished", value.serialize());
}

std::string Service::statusJson() const
{
    base::json::Value value = base::json::Value::object();
    value.insert("name", m_config.name());
    value.insert("port", m_config.port());
    value.insert("visible", m_config.visible());
    value.insert("ask", m_config.askBeforeReceiving());
    value.insert("downloads", m_config.downloads());
    value.insert("fingerprint", m_certificate.fingerprint);
    value.insert("protocol", "https");
    return value.serialize();
}

std::string Service::peersJson() const
{
    return m_discovery.peersJson();
}

std::string Service::choosePath(const Device &sender, const std::string &name) const
{
    std::error_code error;
    const std::string folderName = sanitizedName(sender.alias, "Ripple");
    const std::filesystem::path folder = std::filesystem::path(m_config.downloads()) / folderName;
    std::filesystem::create_directories(folder, error);

    const std::string fileName = sanitizedName(name, "file");
    std::filesystem::path target = folder / fileName;
    if (!std::filesystem::exists(target, error))
        return target.string();

    const std::filesystem::path stem = target.stem();
    const std::filesystem::path extension = target.extension();
    for (int copy = 1; copy < 1000; ++copy) {
        target = folder / (stem.string() + " (" + std::to_string(copy) + ")" + extension.string());
        if (!std::filesystem::exists(target, error))
            break;
    }
    return target.string();
}

HttpResponse Service::handleRequest(HttpRequest &request, Stream &body)
{
    const std::string &path = request.path;

    if (request.method == "GET" && path == "/api/localsend/v2/info")
        return HttpResponse::json(200, self().toJson().serialize());

    if (request.method != "POST")
        return HttpResponse::json(404, "{}");

    if (path == "/api/localsend/v2/register") {
        std::string payload;
        if (readBody(body, request, kMaxRegisterBytes, payload)) {
            if (const auto parsed = base::json::Value::parse(payload)) {
                if (auto device = Device::fromJson(*parsed)) {
                    device->host = request.peer;
                    m_discovery.remember(*device);
                }
            }
        }
        return HttpResponse::json(200, self().toJson().serialize());
    }

    if (path == "/api/localsend/v2/prepare-upload")
        return prepareUpload(request, body);

    if (path == "/api/localsend/v2/upload")
        return upload(request, body);

    if (path == "/api/localsend/v2/cancel") {
        const std::string session = request.queryValue("sessionId");
        std::shared_ptr<Incoming> incoming;
        {
            const std::lock_guard lock(m_sessionsMutex);
            const auto it = m_incoming.find(session);
            if (it != m_incoming.end()) {
                incoming = it->second;
                m_incoming.erase(it);
            }
        }
        if (incoming) {
            const std::lock_guard lock(incoming->mutex);
            incoming->decided = true;
            incoming->acceptedAll = false;
            incoming->answered.notify_all();
        }
        return HttpResponse::json(200, "{}");
    }

    return HttpResponse::json(404, "{}");
}

HttpResponse Service::prepareUpload(HttpRequest &request, Stream &body)
{
    std::string payload;
    if (!readBody(body, request, kMaxPrepareBytes, payload))
        return HttpResponse::json(400, "{}");

    const auto parsed = base::json::Value::parse(payload);
    if (!parsed || !parsed->isObject())
        return HttpResponse::json(400, "{}");

    std::optional<Device> sender = Device::fromJson((*parsed)["info"]);
    if (!sender)
        return HttpResponse::json(400, "{}");
    sender->host = request.peer;
    m_discovery.remember(*sender);

    auto incoming = std::make_shared<Incoming>();
    incoming->session = base::Uuid::generate().toString();
    incoming->sender = *sender;

    const base::json::Value &files = (*parsed)["files"];
    if (!files.isObject())
        return HttpResponse::json(400, "{}");
    for (const auto &[key, value] : files.toObject()) {
        FileInfo file;
        file.id = value["id"].toString(key);
        file.name = value["fileName"].toString("file");
        file.type = value["fileType"].toString("application/octet-stream");
        const int64_t size = value["size"].toInt(0);
        file.size = size > 0 ? uint64_t(size) : 0;
        if (value["sha256"].isString())
            file.sha256 = value["sha256"].toString();
        incoming->total += file.size;
        incoming->tokens[file.id] = base::Uuid::generate().toString();
        incoming->paths[file.id] = choosePath(*sender, file.name);
        incoming->files.push_back(std::move(file));
    }

    {
        const std::lock_guard lock(m_sessionsMutex);
        m_incoming[incoming->session] = incoming;
    }

    base::json::Value truth = base::json::Value::object();
    truth.insert("session", incoming->session);
    truth.insert("sender", sender->toJson());
    truth.insert("ask", m_config.askBeforeReceiving());
    base::json::Value::Array filesArray;
    for (const FileInfo &file : incoming->files) {
        base::json::Value value = base::json::Value::object();
        value.insert("id", file.id);
        value.insert("name", file.name);
        value.insert("size", int64_t(file.size));
        value.insert("type", file.type);
        filesArray.push_back(std::move(value));
    }
    truth.insert("files", base::json::Value(std::move(filesArray)));

    std::string sessionJson = truth.serialize();
    const bool ask = m_config.askBeforeReceiving();
    m_handle.post([this, incoming, sessionJson, ask] {
        if (m_object)
            m_object->emitSignal("incoming", sessionJson);
        if (!ask) {
            {
                const std::lock_guard lock(incoming->mutex);
                incoming->acceptedAll = true;
                for (const FileInfo &file : incoming->files)
                    incoming->accepted.insert(file.id);
                incoming->decided = true;
            }
            incoming->answered.notify_all();
        }
    });

    std::unique_lock lock(incoming->mutex);
    const bool answered = incoming->answered.wait_for(lock, std::chrono::seconds(120),
                                                      [&incoming] { return incoming->decided; });
    if (!answered || !incoming->acceptedAll) {
        {
            const std::lock_guard sessions(m_sessionsMutex);
            m_incoming.erase(incoming->session);
        }
        return HttpResponse::json(answered ? 403 : 408, "{}");
    }

    base::json::Value reply = base::json::Value::object();
    reply.insert("sessionId", incoming->session);
    base::json::Value tokens = base::json::Value::object();
    for (const std::string &id : incoming->accepted) {
        const auto token = incoming->tokens.find(id);
        if (token != incoming->tokens.end())
            tokens.insert(id, token->second);
    }
    reply.insert("files", std::move(tokens));
    return HttpResponse::json(200, reply.serialize());
}

HttpResponse Service::upload(HttpRequest &request, Stream &body)
{
    const std::string session = request.queryValue("sessionId");
    const std::string fileId = request.queryValue("fileId");
    const std::string token = request.queryValue("token");

    std::shared_ptr<Incoming> incoming;
    {
        const std::lock_guard lock(m_sessionsMutex);
        const auto it = m_incoming.find(session);
        if (it == m_incoming.end())
            return HttpResponse::json(403, "{}");
        incoming = it->second;
    }

    std::string path;
    uint64_t total = 0;
    std::string name;
    {
        const std::lock_guard lock(incoming->mutex);
        const auto expected = incoming->tokens.find(fileId);
        if (expected == incoming->tokens.end() || expected->second != token
            || !incoming->accepted.contains(fileId))
            return HttpResponse::json(403, "{}");
        const auto target = incoming->paths.find(fileId);
        if (target == incoming->paths.end())
            return HttpResponse::json(400, "{}");
        path = target->second;
        total = incoming->total;
        for (const FileInfo &file : incoming->files) {
            if (file.id == fileId) {
                name = file.name;
                break;
            }
        }
    }

    auto last = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
    const std::string sessionId = session;
    const bool written = readBodyToFile(body, request, path, [this, incoming, name, total, last,
                                                              sessionId](uint64_t done) {
        const auto now = std::chrono::steady_clock::now();
        if (now - *last < std::chrono::milliseconds(120))
            return;
        *last = now;
        TransferUpdate update;
        update.session = sessionId;
        update.direction = "receive";
        update.file = name;
        update.done = done;
        update.total = total;
        const auto copy = update;
        m_handle.post([this, copy] { emitProgress(copy); });
    });
    if (!written)
        return HttpResponse::json(500, "{}");

    bool finished = false;
    {
        const std::lock_guard lock(incoming->mutex);
        incoming->accepted.erase(fileId);
        finished = incoming->accepted.empty();
    }
    if (finished) {
        std::vector<std::string> received;
        for (const FileInfo &file : incoming->files) {
            const auto target = incoming->paths.find(file.id);
            if (target != incoming->paths.end())
                received.push_back(target->second);
        }
        const std::string sender = incoming->sender.alias;
        {
            const std::lock_guard lock(m_sessionsMutex);
            m_incoming.erase(session);
        }
        const std::string folder = received.empty()
                ? std::string()
                : std::filesystem::path(received.front()).parent_path().string();
        m_handle.post([this, sessionId, sender, received, folder] {
            emitFinished(sessionId, true, {}, folder);
            notifyReceived(sender, received);
        });
    }
    return HttpResponse::json(200, "{}");
}

void Service::respond(const std::string &session, const std::string &acceptedJson)
{
    std::shared_ptr<Incoming> incoming;
    {
        const std::lock_guard lock(m_sessionsMutex);
        const auto it = m_incoming.find(session);
        if (it == m_incoming.end())
            return;
        incoming = it->second;
    }
    std::set<std::string> accepted;
    if (const auto parsed = base::json::Value::parse(acceptedJson)) {
        if (parsed->isArray()) {
            for (const base::json::Value &value : parsed->toArray()) {
                if (value.isString())
                    accepted.insert(value.toString());
            }
        }
    }
    {
        const std::lock_guard lock(incoming->mutex);
        if (incoming->decided)
            return;
        incoming->accepted = std::move(accepted);
        incoming->acceptedAll = !incoming->accepted.empty();
        incoming->decided = true;
    }
    incoming->answered.notify_all();
}

void Service::cancel(const std::string &session)
{
    {
        const std::lock_guard lock(m_sessionsMutex);
        const auto it = m_outgoing.find(session);
        if (it != m_outgoing.end())
            it->second->cancelled = true;
    }
    std::shared_ptr<Incoming> incoming;
    {
        const std::lock_guard lock(m_sessionsMutex);
        const auto it = m_incoming.find(session);
        if (it != m_incoming.end()) {
            incoming = it->second;
            m_incoming.erase(it);
        }
    }
    if (incoming) {
        const std::lock_guard lock(incoming->mutex);
        incoming->decided = true;
        incoming->acceptedAll = false;
        incoming->answered.notify_all();
    }
}

std::string Service::send(const std::string &pathsJson, const std::string &fingerprint)
{
    std::vector<std::string> paths;
    if (const auto parsed = base::json::Value::parse(pathsJson)) {
        if (parsed->isArray()) {
            for (const base::json::Value &value : parsed->toArray()) {
                if (value.isString())
                    paths.push_back(value.toString());
            }
        }
    }
    if (paths.empty())
        return {};

    std::optional<Device> peer;
    for (const Device &device : m_discovery.peers()) {
        if (device.fingerprint == fingerprint) {
            peer = device;
            break;
        }
    }
    if (!peer)
        return {};

    auto outgoing = std::make_shared<Outgoing>();
    outgoing->session = base::Uuid::generate().toString();
    outgoing->peer = *peer;
    const std::string session = outgoing->session;
    {
        const std::lock_guard lock(m_sessionsMutex);
        m_outgoing[session] = outgoing;
    }
    std::thread worker([this, outgoing, paths] { sendWorker(outgoing, paths); });
    {
        const std::lock_guard lock(m_sessionsMutex);
        m_workers.push_back(std::move(worker));
    }
    return session;
}

void Service::sendWorker(std::shared_ptr<Outgoing> outgoing, std::vector<std::string> paths)
{
    CF_INFO(cfRipple) << "send to" << outgoing->peer.host << outgoing->peer.port << "paths"
                      << paths.size();
    struct Item
    {
        FileInfo info;
        std::string path;
    };
    std::vector<Item> items;
    uint64_t total = 0;
    for (size_t index = 0; index < paths.size(); ++index) {
        const std::filesystem::path path(paths[index]);
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
            continue;
        Item item;
        item.path = path.string();
        item.info.id = "f" + std::to_string(index);
        item.info.name = path.filename().string();
        item.info.type = mimeFor(item.info.name);
        item.info.size = uint64_t(std::filesystem::file_size(path, error));
        if (error)
            item.info.size = 0;
        if (item.info.size <= 64 * 1024 * 1024) {
            const std::string digest = sha256File(item.path);
            if (!digest.empty())
                item.info.sha256 = digest;
        }
        total += item.info.size;
        items.push_back(std::move(item));
    }

    if (items.empty()) {
        m_handle.post([this, session = outgoing->session] {
            emitFinished(session, false, "nothing to send");
            const std::lock_guard lock(m_sessionsMutex);
            m_outgoing.erase(session);
        });
        return;
    }

    base::json::Value request = base::json::Value::object();
    request.insert("info", self().toJson());
    base::json::Value files = base::json::Value::object();
    for (const Item &item : items) {
        base::json::Value value = base::json::Value::object();
        value.insert("id", item.info.id);
        value.insert("fileName", item.info.name);
        value.insert("size", int64_t(item.info.size));
        value.insert("fileType", item.info.type);
        if (item.info.sha256)
            value.insert("sha256", *item.info.sha256);
        files.insert(item.info.id, std::move(value));
    }
    request.insert("files", std::move(files));

    CF_INFO(cfRipple) << "prepare-upload with" << items.size() << "file(s)," << total << "bytes";
    std::string reply, error;
    const int status = m_client.postJson(outgoing->peer, "/api/localsend/v2/prepare-upload",
                                         request.serialize(), reply, error);
    if (status != 200) {
        const std::string reason = !error.empty() ? error
                : status == 401 ? "the device asks for a PIN"
                : status == 403 ? "the transfer was rejected"
                : status == 409 ? "the device is busy"
                : "the device did not answer";
        m_handle.post([this, session = outgoing->session, reason] {
            emitFinished(session, false, reason);
            const std::lock_guard lock(m_sessionsMutex);
            m_outgoing.erase(session);
        });
        return;
    }

    CF_INFO(cfRipple) << "prepare-upload status" << status << error << reply;
    const auto parsed = base::json::Value::parse(reply);
    const std::string sessionId = parsed ? (*parsed)["sessionId"].toString() : std::string();
    if (!parsed || sessionId.empty()) {
        m_handle.post([this, session = outgoing->session] {
            emitFinished(session, false, "the device's answer was not understood");
            const std::lock_guard lock(m_sessionsMutex);
            m_outgoing.erase(session);
        });
        return;
    }

    uint64_t carried = 0;
    for (const Item &item : items) {
        if (outgoing->cancelled)
            break;
        const std::string token = (*parsed)["files"][item.info.id].toString();
        if (token.empty())
            continue;

        const std::string path = "/api/localsend/v2/upload?sessionId=" + sessionId + "&fileId="
                + item.info.id + "&token=" + token;
        auto last = std::make_shared<std::chrono::steady_clock::time_point>(
                std::chrono::steady_clock::now());
        const uint64_t before = carried;
        const std::string session = outgoing->session;
        const std::string name = item.info.name;
        const int uploadStatus = m_client.postFile(
                outgoing->peer, path, item.path,
                [this, session, name, before, total, last](uint64_t sent) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now - *last < std::chrono::milliseconds(120))
                        return;
                    *last = now;
                    TransferUpdate update;
                    update.session = session;
                    update.direction = "send";
                    update.file = name;
                    update.done = before + sent;
                    update.total = total;
                    const auto copy = update;
                    m_handle.post([this, copy] { emitProgress(copy); });
                },
                error);
        if (uploadStatus != 200) {
            m_handle.post([this, session = outgoing->session, error] {
                emitFinished(session, false, error.empty() ? "the file was not accepted" : error);
                const std::lock_guard lock(m_sessionsMutex);
                m_outgoing.erase(session);
            });
            return;
        }
        carried += item.info.size;
    }

    const bool cancelled = outgoing->cancelled;
    m_handle.post([this, session = outgoing->session, cancelled] {
        emitFinished(session, !cancelled, cancelled ? "cancelled" : std::string());
        const std::lock_guard lock(m_sessionsMutex);
        m_outgoing.erase(session);
    });
}

void Service::notifyReceived(const std::string &sender, const std::vector<std::string> &paths)
{
    if (!m_notifications || paths.empty() || m_panelVisible)
        return;

    const std::string folder = std::filesystem::path(paths.front()).parent_path().string();
    std::string summary;
    std::string body;
    if (paths.size() == 1) {
        summary = tr("File received");
        body = std::filesystem::path(paths.front()).filename().string();
    } else {
        summary = trn("{n} file received", "{n} files received", uint64_t(paths.size()));
    }
    if (!sender.empty()) {
        const std::string from = format(tr("from {name}"), {{"name", sender}});
        body = body.empty() ? from : body + " — " + from;
    }

    std::vector<std::string> actions = {"default", "", "show-in-folder", tr("Show in Files")};
    base::dbus::VariantMap hints;
    hints["desktop-entry"] = base::dbus::Variant(std::string("cutefish-files"));
    hints["urgency"] = base::dbus::Variant(uint8_t(1));

    const auto id = m_notifications
                            ->call("Notify", std::string("Ripple"), uint32_t(0), std::string("folder-download"),
                                   summary, body, actions, hints, int32_t(8000))
                            .value<uint32_t>();
    if (id && *id != 0)
        m_notified[*id] = {folder, paths.front()};
}

void Service::notifyOffer(const std::string &session)
{
    if (!m_notifications)
        return;
    std::shared_ptr<Incoming> incoming;
    {
        const std::lock_guard lock(m_sessionsMutex);
        const auto it = m_incoming.find(session);
        if (it == m_incoming.end())
            return;
        incoming = it->second;
    }
    std::vector<std::string> ids;
    {
        const std::lock_guard lock(incoming->mutex);
        if (incoming->decided)
            return;
        for (const FileInfo &file : incoming->files)
            ids.push_back(file.id);
    }

    const std::string summary = format(tr("{name} wants to send"),
                                       {{"name", incoming->sender.alias}});
    const std::string body = trn("{n} file", "{n} files", uint64_t(ids.size()));
    std::vector<std::string> actions = {"accept", tr("Accept"), "decline", tr("Decline")};
    base::dbus::VariantMap hints;
    hints["desktop-entry"] = base::dbus::Variant(std::string("cutefish-files"));
    hints["urgency"] = base::dbus::Variant(uint8_t(1));

    const auto id = m_notifications
                            ->call("Notify", std::string("Ripple"), uint32_t(0), std::string("folder-download"),
                                   summary, body, actions, hints, int32_t(30000))
                            .value<uint32_t>();
    if (id && *id != 0)
        m_offerNotifications[*id] = {session, std::move(ids)};
}

void Service::pruneSessions()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> abandoned;
    {
        const std::lock_guard lock(m_sessionsMutex);
        for (auto it = m_incoming.begin(); it != m_incoming.end();) {
            const std::lock_guard guard(it->second->mutex);
            if (it->second->decided && now - it->second->created > std::chrono::minutes(10)) {
                abandoned.push_back(it->first);
                it = m_incoming.erase(it);
            } else if (!it->second->decided && now - it->second->created > std::chrono::minutes(5)) {
                abandoned.push_back(it->first);
                it = m_incoming.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const std::string &session : abandoned)
        emitFinished(session, false, "the transfer timed out");
}

} // namespace cutefish::ripple
