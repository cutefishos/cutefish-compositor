// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <csignal>
#include <memory>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "base/event_loop.h"
#include "base/i18n.h"
#include "base/locale.h"
#include "core/compositor_options.h"
#include "core/compositor_shell.h"
#include "core/logging.h"
#include "core/main_loop.h"
#include "core/malloc_policy.h"
#include "core/scoped_fd.h"
#include "scene/motion.h"

namespace {

// SIGHUP comes when the session leader exits; dying of it would skip the
// teardown that leaves the last frame on screen.
int createSignalFd()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) != 0)
        return -1;

    return signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
}

} // namespace

int main(int argc, char *argv[])
{
    cutefish::core::applyMallocPolicy();
    signal(SIGPIPE, SIG_IGN);
    // Set now, not only once the embedded toolkit sets it for the lock screen.
    cutefish::base::Locale::setProcessLocale();
    cutefish::i18n::setDomain("cutefish-compositor");
    const cutefish::ScopedFd signalFdOwner(createSignalFd());
    const int signalFd = signalFdOwner.get();
    // Outlives the shell, whose objects may watch, time or post on it.
    const cutefish::core::MainLoop mainLoop;

    const auto options = cutefish::core::CompositorOptions::parse(argc, argv);
    if (!options)
        return 1;
    if (!options->logRules.empty())
        cutefish::core::setLoggingRules(options->logRules);
    cutefish::scene::motion::setDurationScale(options->animationDurationScale);

    auto shell = std::make_unique<cutefish::core::CompositorShell>(*options);
    if (!shell->init())
        return 1;

    int signalWatch = 0;
    if (signalFd >= 0) {
        signalWatch = mainLoop.loop().watch(signalFd, EPOLLIN, [&](uint32_t) {
            signalfd_siginfo info;
            while (read(signalFd, &info, sizeof(info)) == sizeof(info))
                ;
            CF_INFO(cfWayland) << "Shutting down";
            shell->stop();
            mainLoop.loop().quit();
        });
    } else {
        CF_WARNING(cfWayland) << "Failed to create signalfd; Ctrl-C will not shut down cleanly";
    }

    CF_INFO(cfWayland) << "cutefish-compositor started, WAYLAND_DISPLAY=" + shell->socketName();

    if (!shell->notifyReady())
        return 1;

    const int result = mainLoop.loop().run();
    shell.reset();

    if (signalWatch)
        mainLoop.loop().unwatch(signalWatch);
    return result;
}
