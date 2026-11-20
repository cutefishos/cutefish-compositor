// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SCREENSHOT_CAPTURE_NOTIFIER_H_
#define CUTEFISH_SHELL_SCREENSHOT_CAPTURE_NOTIFIER_H_

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/thread_pool.h"
#include "shell/common/pending_calls.h"
#include "ui/bitmap.h"

namespace cutefish::shell {

class CaptureNotifier
{
public:
    explicit CaptureNotifier(base::dbus::Connection *bus);
    ~CaptureNotifier();

    // The sound theme's "screen-capture" event, played in a process of its own.
    void playShutter();
    void notifySaved(const std::string &path, bool video);
    void notifyCopied(const ui::Bitmap &image);
    void notifyTaken(const ui::Bitmap &image);
    void notifyFailed(const std::string &summary, const std::string &reason);

    // Empty when the theme has none.
    static std::string soundFile(const std::string &event);
    std::function<void(const std::string &file)> playSound;

    // At least 132 px both ways, as the notification centre crops it, unless the
    // picture is smaller.
    static ui::Bitmap thumbnail(const ui::Bitmap &image);

private:
    void notify(const std::string &summary, const std::string &body, const std::string &image,
                const std::string &path, const ui::Bitmap &thumbnail = {});
    void notifyPicture(const std::string &body, const ui::Bitmap &image);
    void actionInvoked(uint32_t id, const std::string &action);

    base::dbus::Connection *m_bus;
    base::dbus::Proxy m_notifications;
    base::dbus::Match m_actionInvoked;
    base::dbus::Match m_closed;
    std::map<uint32_t, std::string> m_paths;
    std::vector<base::Job> m_thumbnails;
    PendingCalls m_calls;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SCREENSHOT_CAPTURE_NOTIFIER_H_
