// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SCREENSHOT_SCREEN_RECORDING_H_
#define CUTEFISH_SHELL_SCREENSHOT_SCREEN_RECORDING_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "base/timer.h"

namespace cutefish::base {
class Process;
}

namespace cutefish::shell {

// "started" on its output once the first frame is in; "stop" or the end of its
// input ends it.
class ScreenRecording
{
public:
    struct Target {
        std::string output;
        // Global logical coordinates; empty for the whole output.
        base::Rect region;
        uint64_t window = 0;
        bool cursor = true;
        // A PipeWire node name; empty for no sound.
        std::string microphone;
    };

    // -1 when there is none.
    using Connect = std::function<int()>;

    // Empty `environment` keeps this process's; without `connect` the recorder finds
    // the compositor on its own.
    ScreenRecording(std::string program, std::vector<std::string> environment, Connect connect = {});
    ~ScreenRecording();

    bool isRunning() const { return m_process != nullptr; }
    const std::string &path() const { return m_path; }

    static std::vector<std::string> arguments(const Target &target, const std::string &path);

    bool start(const Target &target, const std::string &path);
    void stop();

    base::Signal<> recordingChanged;
    base::Signal<bool, std::string> finished;
    std::function<void(int64_t pid)> onStarted;
    std::function<void(int64_t pid)> onStopped;

private:
    void onOutput(std::string_view output);
    void onFinished(int exitCode, bool crashed);

    const std::string m_program;
    const std::vector<std::string> m_environment;
    const Connect m_connect;
    std::unique_ptr<base::Process> m_process;
    int64_t m_pid = 0;
    std::string m_path;
    std::string m_output;
    bool m_recording = false;
    base::Timer m_killTimer;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SCREENSHOT_SCREEN_RECORDING_H_
