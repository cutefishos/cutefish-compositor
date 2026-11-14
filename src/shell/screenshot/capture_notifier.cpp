// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/screenshot/capture_notifier.h"

#include <algorithm>
#include <filesystem>
#include <tuple>

#include "base/i18n.h"
#include "base/paths.h"
#include "base/process.h"
#include "base/url.h"

using cutefish::i18n::tr;

namespace cutefish::shell {

namespace dbus = base::dbus;

static const char kService[] = "org.freedesktop.Notifications";
static const char kPath[] = "/org/freedesktop/Notifications";
static const char kShowInFolder[] = "show-in-folder";
// The notification centre keeps a 132 px square cut from the middle; the
// picture only needs to cover it.
static constexpr int kThumbnailSize = 132;
// The freedesktop sound theme, which libcanberra plays from without a theme
// set; the variant directories it looks in, best first.
static const char kSoundTheme[] = "freedesktop";

using ImageData = std::tuple<int32_t, int32_t, int32_t, bool, int32_t, int32_t, std::vector<uint8_t>>;

static std::string fileName(const std::string &path)
{
    return std::filesystem::path(path).filename().string();
}

static std::string folderOf(const std::string &path)
{
    return std::filesystem::path(path).parent_path().string();
}

CaptureNotifier::CaptureNotifier(dbus::Connection *bus)
    : m_bus(bus)
{
    if (!m_bus)
        return;
    m_notifications = dbus::Proxy(m_bus, kService, kPath, kService);
    m_actionInvoked = m_notifications.onSignal("ActionInvoked", [this](const dbus::Message &message) {
        uint32_t id = 0;
        std::string action;
        if (message.read(id, action))
            actionInvoked(id, action);
    });
    m_closed = m_notifications.onSignal("NotificationClosed", [this](const dbus::Message &message) {
        uint32_t id = 0;
        if (message.read(id))
            m_paths.erase(id);
    });
}

CaptureNotifier::~CaptureNotifier() = default;

std::string CaptureNotifier::soundFile(const std::string &event)
{
    std::vector<std::string> roots = {base::paths::dataHome()};
    for (const std::string &directory : base::paths::dataDirectories())
        roots.push_back(directory);
    for (const std::string &root : roots) {
        for (const char *variant : {"/stereo/", "/"}) {
            for (const char *extension : {".oga", ".ogg", ".wav"}) {
                const std::string path = root + "/sounds/" + kSoundTheme + variant + event + extension;
                std::error_code error;
                if (std::filesystem::is_regular_file(path, error))
                    return path;
            }
        }
    }
    return {};
}

void CaptureNotifier::playShutter()
{
    const std::string file = soundFile("screen-capture");
    if (file.empty())
        return;
    if (playSound) {
        playSound(file);
        return;
    }
    base::Process::startDetached("pw-play", {file});
}

void CaptureNotifier::notifySaved(const std::string &path, bool video)
{
    notify(video ? tr("Screen Recording") : tr("Screenshot"), fileName(path), video ? std::string() : path, path);
}

ui::Bitmap CaptureNotifier::thumbnail(const ui::Bitmap &image)
{
    if (image.width() <= kThumbnailSize || image.height() <= kThumbnailSize)
        return image;
    // The smallest size of the same aspect ratio that covers the box.
    const int64_t width = image.width();
    const int64_t height = image.height();
    const int64_t scaledWidth = int64_t(kThumbnailSize) * width / height;
    if (scaledWidth >= kThumbnailSize)
        return image.scaled(int(scaledWidth), kThumbnailSize, true);
    return image.scaled(kThumbnailSize, int(int64_t(kThumbnailSize) * height / width), true);
}

void CaptureNotifier::notifyCopied(const ui::Bitmap &image)
{
    notifyPicture(tr("Copied to the clipboard"), image);
}

void CaptureNotifier::notifyTaken(const ui::Bitmap &image)
{
    notifyPicture(tr("Taken from the command line"), image);
}

void CaptureNotifier::notifyPicture(const std::string &body, const ui::Bitmap &image)
{
    m_thumbnails.push_back(base::runInThread([image] { return thumbnail(image); },
                                             [this, body](ui::Bitmap small) {
                                                 notify(tr("Screenshot"), body, {}, {}, small);
                                                 std::erase_if(m_thumbnails, [](const base::Job &job) {
                                                     return !job.isPending();
                                                 });
                                             }));
}

void CaptureNotifier::notifyFailed(const std::string &summary, const std::string &reason)
{
    notify(summary, reason, {}, {});
}

void CaptureNotifier::notify(const std::string &summary, const std::string &body, const std::string &image,
                             const std::string &path, const ui::Bitmap &thumbnail)
{
    if (!m_bus)
        return;
    dbus::VariantMap hints;
    hints["urgency"] = dbus::Variant(uint8_t(1));
    // Under the Screenshot entry in the launchpad, for its name, icon and settings.
    hints["desktop-entry"] = dbus::Variant(std::string("cutefish-screenshot"));
    if (!image.empty())
        hints["image-path"] = dbus::Variant(image);
    if (!thumbnail.isNull()) {
        // The specification's (iiibiiay): width, height, rowstride, alpha,
        // bits per sample, channels and RGBA bytes.
        const ui::Bitmap rgba = thumbnail.converted(ui::Bitmap::Format::Rgba8888Premultiplied);
        std::vector<uint8_t> bytes(rgba.constBits(), rgba.constBits() + rgba.sizeInBytes());
        // The specification's bytes are not premultiplied.
        for (size_t i = 0; i + 3 < bytes.size(); i += 4) {
            const int alpha = bytes[i + 3];
            if (alpha == 0 || alpha == 255)
                continue;
            for (size_t c = 0; c < 3; ++c)
                bytes[i + c] = uint8_t(std::min(255, (bytes[i + c] * 255 + alpha / 2) / alpha));
        }
        hints["image-data"] = dbus::Variant(ImageData(rgba.width(), rgba.height(), int32_t(rgba.stride()), true,
                                                      8, 4, std::move(bytes)));
    }
    std::vector<std::string> actions;
    if (!path.empty())
        actions = {"default", "", kShowInFolder, tr("Show in Folder")};
    m_calls.track([&](std::function<void()> finished) {
        return m_notifications.callAsync(
                "Notify",
                [this, path, finished](const dbus::Reply &reply) {
                    const std::optional<uint32_t> id = reply.value<uint32_t>();
                    // 0: the server dropped it.
                    if (id && *id && !path.empty())
                        m_paths[*id] = path;
                    finished();
                },
                std::string(), uint32_t(0), std::string("accessories-screenshot"), summary, body, actions,
                hints, int32_t(-1));
    });
}

void CaptureNotifier::actionInvoked(uint32_t id, const std::string &action)
{
    const auto it = m_paths.find(id);
    if (it == m_paths.end())
        return;
    const std::string path = it->second;
    if (action == kShowInFolder) {
        const dbus::Proxy fileManager(m_bus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                                      "org.freedesktop.FileManager1");
        m_calls.track([&](std::function<void()> finished) {
            return fileManager.callAsync(
                    "ShowItems",
                    [path, finished](const dbus::Reply &reply) {
                        // Without a running file manager to ask, open the folder instead.
                        if (reply.isError())
                            base::Process::startDetached("xdg-open", {folderOf(path)});
                        finished();
                    },
                    std::vector<std::string> {base::url::fromLocalFile(path)}, std::string());
        });
    } else if (action == "default") {
        base::Process::startDetached("xdg-open", {path});
    }
}

} // namespace cutefish::shell
