// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <cstdio>

#include "base/event_loop.h"
#include "base/i18n.h"
#include "ripple/service.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    cutefish::i18n::setDomain("cutefish-compositor");

    // Signals are taken by a descriptor the loop watches, so a stop request
    // and a new announcement are handled in the same place.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) {
        std::fprintf(stderr, "cutefish-ripple: cannot block signals\n");
        return 1;
    }
    const int signalFd = signalfd(-1, &mask, SFD_CLOEXEC);

    cutefish::base::EventLoop loop;
    cutefish::ripple::Service service(loop);
    if (!service.start()) {
        std::fprintf(stderr, "cutefish-ripple: cannot start\n");
        return 1;
    }

    int watch = 0;
    if (signalFd >= 0) {
        watch = loop.watch(signalFd, EPOLLIN, [&loop, signalFd](uint32_t) {
            signalfd_siginfo info {};
            if (::read(signalFd, &info, sizeof(info)) == ssize_t(sizeof(info)))
                loop.quit();
        });
    }
    (void)watch;

    return loop.run();
}
