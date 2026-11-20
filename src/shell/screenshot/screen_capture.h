// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SCREENSHOT_SCREEN_CAPTURE_H_
#define CUTEFISH_SHELL_SCREENSHOT_SCREEN_CAPTURE_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "shell/shell.h"
#include "ui/bitmap.h"

namespace cutefish::base::dbus {
class Object;
}

namespace cutefish::clipboard {
class SelectionController;
}

namespace cutefish::compositor {
class CaptureSource;
}

namespace cutefish::ui {
class FileChooser;
}

namespace cutefish::wm {
class Window;
}

namespace cutefish::shell {

class CaptureClipboard;
class CaptureNotifier;
class CaptureOverlay;
class CaptureSettings;
class ScreenGrabber;
class ScreenRecording;

struct CaptureDock {
    wm::Window *window = nullptr;
    base::Rect geometry;
    double radius = 0;
    bool bottom = true;
};

struct ScreenCaptureServices {
    std::unique_ptr<ScreenGrabber> grabber;
    clipboard::SelectionController *selection = nullptr;
    // shell.conf.
    std::string settingsPath;
    // Unset, it finds the compositor on its own.
    std::string recorder;
    std::vector<std::string> recorderEnvironment;
    std::function<int()> recorderConnection;
    std::function<std::optional<CaptureDock>()> dock;
    // Unset or null captures the window alone.
    std::function<std::shared_ptr<compositor::CaptureSource>(wm::Window *desktop, const std::string &output)>
            desktop;
};

// One Capture window per output, never in what is captured.
class ScreenCapture : public Component
{
public:
    enum Type {
        Image,
        Video,
    };

    enum Source {
        Screen,
        Window,
        Region,
    };

    enum class PickedKind {
        None,
        Window,
        Desktop,
        Panel,
        Dock,
    };

    static constexpr int kToolbarGap = 12;
    static constexpr int kToolbarScreenMargin = 32;
    static constexpr std::chrono::milliseconds kPointerSettle {150};
    // The compositor's rounded window corners.
    static constexpr double kWindowRadius = 14;
    // Captures in a row reuse the overlays.
    static constexpr std::chrono::seconds kOverlayExpiry {60};

    ScreenCapture(Shell *shell, ScreenCaptureServices services);
    ~ScreenCapture() override;

    bool registerService();

    void takeScreenshot(bool toClipboard);
    void takePartialScreenshot(bool toClipboard);
    void startCaptureMode();
    void stopRecording();
    bool isRecording() const;
    // Taken at once, with the shutter and a notification, so no capture goes unseen.
    // An empty name means the only output.
    using PictureDone = std::function<void(std::optional<std::string> png, const std::string &error)>;
    void captureOutput(const std::string &output, bool cursor, PictureDone done);

    bool isActive() const { return m_active; }
    bool isToolbarStyle() const { return m_active && m_toolbar; }
    int type() const { return m_type; }
    int source() const { return m_source; }
    // Global logical coordinates.
    base::Rect selection() const { return m_selection; }
    base::Rect hoveredWindow() const { return m_picked.geometry; }
    double hoveredWindowRadius() const { return m_picked.radius; }
    std::string toolbarOutput() const;
    base::Rect toolbarArea() const;
    int countdown() const { return m_countdown; }
    base::Rect countdownArea() const { return m_countdownArea; }
    CaptureSettings &settings() { return *m_settings; }
    CaptureNotifier &notifier() { return *m_notifier; }
    const std::vector<std::unique_ptr<CaptureOverlay>> &overlays() const { return m_overlays; }
    CaptureOverlay *overlay(const std::string &output) const;

    void setMode(int type, int source);
    void cancel();
    void toggleWindowSelection();
    void pointerMoved(const base::Point &position);
    void setSelection(const base::Rect &selection);
    void finishSelection(const base::Rect &selection, bool toClipboard);
    void clickAt(const base::Point &position, const std::string &output, bool toClipboard);
    void perform(bool toClipboard);
    void chooseFolder();

    base::Signal<> recordingChanged;
    std::function<void(const std::vector<std::string> &paths)> onSaved;

    // Shared with the workers that pick them.
    struct SavingNames;

private:
    friend class ScreenCaptureTestAccess;

    enum class Style {
        Toolbar,
        Selection,
    };

    struct Target {
        std::vector<std::string> outputs;
        uint64_t window = 0;
        // Global logical coordinates, within outputs.front().
        base::Rect region;
        bool dock = false;
        uint64_t desktop = 0;
    };

    struct Picked {
        PickedKind kind = PickedKind::None;
        uint64_t id = 0;
        base::Rect geometry;
        double radius = 0;
        std::string output;

        bool isValid() const { return !geometry.isEmpty(); }
        bool operator==(const Picked &) const = default;
    };

    void begin(Style style);
    void end();
    void syncOverlays();
    void notifyChanged();
    void pick();
    Picked resolve(const base::Point &point) const;
    void setPicked(const Picked &picked);
    Target targetFor(const Picked &picked) const;
    base::Rect defaultSelection() const;
    const Shell::Output *toolbarScreen() const;
    const Shell::Output *outputAt(const base::Point &point) const;
    std::optional<CaptureDock> dock() const;

    void schedule(const Target &target, bool toClipboard, const base::Rect &area);
    void execute(const Target &target, bool toClipboard);
    void captureImage(const Target &target, bool toClipboard);
    void startRecording(const Target &target);
    void deliverImages(std::vector<ui::Bitmap> images, std::vector<base::Rect> geometries, bool toClipboard);
    void saveImages(std::vector<ui::Bitmap> images);

    Shell *m_shell;
    ScreenCaptureServices m_services;
    std::unique_ptr<CaptureSettings> m_settings;
    std::unique_ptr<CaptureClipboard> m_clipboard;
    std::unique_ptr<CaptureNotifier> m_notifier;
    std::unique_ptr<ScreenRecording> m_recording;
    std::vector<std::unique_ptr<CaptureOverlay>> m_overlays;
    std::unique_ptr<base::dbus::Object> m_service;
    std::unique_ptr<ui::FileChooser> m_chooser;
    std::vector<base::Subscription> m_subscriptions;
    std::vector<base::Job> m_jobs;
    std::shared_ptr<SavingNames> m_saving;
    bool m_recordingStarting = false;
    std::unique_ptr<base::Timer> m_settle;

    bool m_active = false;
    bool m_toolbar = false;
    Style m_style = Style::Toolbar;
    int m_type = Image;
    int m_source = Region;
    bool m_clipboardShortcut = false;
    base::Rect m_selection;
    Picked m_picked;
    // In global coordinates.
    std::optional<base::Point> m_pointer;

    int m_countdown = 0;
    base::Rect m_countdownArea;
    base::Timer m_countdownTimer;
    base::Timer m_overlayExpiry;
    std::chrono::milliseconds m_overlayExpiryDelay = kOverlayExpiry;
    std::function<void()> m_pending;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SCREENSHOT_SCREEN_CAPTURE_H_
