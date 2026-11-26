// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "auth/auth_controller.h"
#include "fake_backend.h"
#include "temporary_directory.h"

#include <algorithm>
#include <cstdlib>

#include <gtest/gtest.h>

using namespace cutefish::auth;
using cutefish::test::TemporaryDirectory;

TEST(AuthControllerTest, LoginStartsTheChosenSession)
{
    FakeBackend backend;
    AuthController controller(AuthController::Login, &backend);
    controller.setSessions({{"gnome", "GNOME", {}, {}}, {"cutefish-wayland", "Cutefish", {}, {}}});
    EXPECT_EQ(controller.sessionNames(), (std::vector<std::string> {"GNOME", "Cutefish"}));
    EXPECT_TRUE(controller.canSelectSession());
    EXPECT_EQ(controller.sessionIndex(), 1);

    controller.setSessionIndex(5);
    EXPECT_EQ(controller.sessionIndex(), 1);
    controller.setSessionIndex(0);
    EXPECT_EQ(controller.sessionIndex(), 0);

    controller.authenticate("hunter2");
    EXPECT_EQ(backend.lastSession, "gnome");
    controller.setSessionIndex(1);
    EXPECT_EQ(controller.sessionIndex(), 0);
}

TEST(AuthControllerTest, LoginOffersTheAccountsInsteadOfATypedName)
{
    FakeBackend backend;
    AuthController controller(AuthController::Login, &backend);
    controller.setUsers({"alice", "tester"});
    EXPECT_EQ(controller.userName(), "tester");
    EXPECT_EQ(controller.userIndex(), 1);
    EXPECT_TRUE(controller.canPickUser());
    EXPECT_FALSE(controller.typesUserName());

    controller.setUserIndex(0);
    EXPECT_EQ(controller.userName(), "alice");
    controller.setUserIndex(7);
    EXPECT_EQ(controller.userName(), "alice");

    controller.setUsers({"bob"});
    EXPECT_EQ(controller.userName(), "bob");
    EXPECT_FALSE(controller.canPickUser());

    controller.setUsers({});
    EXPECT_TRUE(controller.typesUserName());

    FakeBackend lockBackend;
    AuthController lock(AuthController::Lock, &lockBackend);
    EXPECT_FALSE(lock.typesUserName());
    EXPECT_FALSE(lock.canPickUser());
}

TEST(AuthControllerTest, LoginSessionsFollowWaylandSessions)
{
    TemporaryDirectory directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(directory.write(
            "plasma.desktop",
            "[Desktop Entry]\nName=Plasma\nExec=/usr/bin/startplasma-wayland %u\nDesktopNames=KDE\n"));
    ASSERT_TRUE(directory.write("hidden.desktop",
                                "[Desktop Entry]\nName=Hidden\nExec=/bin/true\nHidden=true\n"));
    ASSERT_TRUE(directory.write(
            "gone.desktop", "[Desktop Entry]\nName=Gone\nExec=/bin/true\nTryExec=/nonexistent\n"));
    const auto sessions = findLoginSessions({directory.path()});
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions.front().id, "plasma");
    EXPECT_EQ(sessions.front().command, std::vector<std::string> {"/usr/bin/startplasma-wayland"});
    const auto &environment = sessions.front().environment;
    EXPECT_NE(std::find(environment.begin(), environment.end(), "XDG_SESSION_TYPE=wayland"),
              environment.end());
    EXPECT_NE(std::find(environment.begin(), environment.end(), "XDG_CURRENT_DESKTOP=KDE"),
              environment.end());

    setLoginSessionDirectoriesForTesting({directory.path()});
    FakeBackend backend;
    AuthController controller(AuthController::Login, &backend);
    EXPECT_EQ(controller.sessionNames(), std::vector<std::string> {"Plasma"});
    EXPECT_FALSE(controller.canSelectSession());
    EXPECT_EQ(controller.sessionIndex(), 0);
    setLoginSessionDirectoriesForTesting({});
}

TEST(AuthControllerTest, EveryDesktopNameReachesTheSessionEnvironment)
{
    TemporaryDirectory directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(directory.write("cutefish-wayland.desktop",
                                "[Desktop Entry]\nName=Cutefish\nExec=/bin/true\n"
                                "DesktopNames=CutefishDesktopSession; SecondDesktopName ;;\n"));
    const auto sessions = findLoginSessions({directory.path()});
    ASSERT_EQ(sessions.size(), 1u);
    const auto &environment = sessions.front().environment;
    EXPECT_NE(std::find(environment.begin(), environment.end(),
                        "XDG_SESSION_DESKTOP=CutefishDesktopSession"),
              environment.end());
    EXPECT_NE(std::find(environment.begin(), environment.end(),
                        "XDG_CURRENT_DESKTOP=CutefishDesktopSession:SecondDesktopName"),
              environment.end());
}

TEST(AuthControllerTest, SplitsOnWhitespaceAndQuotes)
{
    EXPECT_EQ(splitCommand("  a  b\tc "), (std::vector<std::string> {"a", "b", "c"}));
    EXPECT_EQ(splitCommand("run \"two words\" x"),
              (std::vector<std::string> {"run", "two words", "x"}));
    EXPECT_EQ(splitCommand("say \"\"\"quoted\"\"\""), (std::vector<std::string> {"say", "\"quoted\""}));
    EXPECT_EQ(splitCommand("a\"b c\"d"), (std::vector<std::string> {"ab cd"}));
    EXPECT_TRUE(splitCommand("").empty());
}

TEST(AuthControllerTest, BothModesShareOneController)
{
    for (auto mode : {AuthController::Login, AuthController::Lock}) {
        FakeBackend backend;
        AuthController controller(mode, &backend);
        controller.start();

        EXPECT_EQ(controller.mode(), mode);
        EXPECT_EQ(controller.state(), AuthController::Idle);
        EXPECT_EQ(backend.startCount, 1);

        controller.authenticate("hunter2");
        EXPECT_EQ(controller.state(), AuthController::Authenticating);
        EXPECT_EQ(backend.authenticateCount, 1);
        EXPECT_EQ(backend.lastSecret, "hunter2");

        backend.succeed();
        EXPECT_EQ(controller.state(), AuthController::Succeeded);
        EXPECT_EQ(backend.acceptCount, 1);
    }
}

TEST(AuthControllerTest, FailureNeverAccepts)
{
    FakeBackend backend;
    AuthController controller(AuthController::Lock, &backend);
    int failures = 0;
    int successes = 0;
    auto failed = controller.authenticationFailed.connect([&] { ++failures; });
    auto succeeded = controller.authenticationSucceeded.connect([&] { ++successes; });

    for (int attempt = 0; attempt < 5; ++attempt) {
        controller.authenticate("wrong");
        backend.reject();
    }

    EXPECT_EQ(failures, 5);
    EXPECT_EQ(successes, 0);
    EXPECT_EQ(backend.acceptCount, 0);
}

TEST(AuthControllerTest, FailureResetsAndClears)
{
    FakeBackend backend;
    AuthController controller(AuthController::Lock, &backend);

    controller.authenticate("wrong");
    backend.reject("Incorrect password");

    EXPECT_EQ(controller.state(), AuthController::Failed);
    EXPECT_EQ(controller.errorMessage(), "Incorrect password");
    EXPECT_FALSE(controller.busy());

    controller.reset();

    EXPECT_EQ(controller.state(), AuthController::Idle);
    EXPECT_TRUE(controller.errorMessage().empty());
    EXPECT_EQ(backend.resetCount, 1);

    controller.authenticate("right");
    backend.succeed();
    EXPECT_EQ(controller.state(), AuthController::Succeeded);
    EXPECT_EQ(backend.acceptCount, 1);
}

TEST(AuthControllerTest, SuccessAcceptsExactlyOnce)
{
    FakeBackend backend;
    AuthController controller(AuthController::Lock, &backend);
    int successes = 0;
    auto succeeded = controller.authenticationSucceeded.connect([&] { ++successes; });

    controller.authenticate("right");

    backend.succeed();
    backend.succeed();
    backend.succeed();

    EXPECT_EQ(successes, 1);
    EXPECT_EQ(backend.acceptCount, 1);
}

TEST(AuthControllerTest, SuccessIsNotUndoneByALateFailure)
{
    FakeBackend backend;
    AuthController controller(AuthController::Lock, &backend);

    controller.authenticate("right");
    backend.succeed();
    backend.reject();

    EXPECT_EQ(controller.state(), AuthController::Succeeded);
    EXPECT_EQ(backend.acceptCount, 1);
}

TEST(AuthControllerTest, ResetAfterSuccessIsRefused)
{
    FakeBackend backend;
    AuthController controller(AuthController::Lock, &backend);

    controller.authenticate("right");
    backend.succeed();
    controller.reset();

    EXPECT_EQ(controller.state(), AuthController::Succeeded);
    EXPECT_EQ(backend.resetCount, 0);
}

TEST(AuthControllerTest, AuthenticateWhileBusyIsIgnored)
{
    FakeBackend backend;
    AuthController controller(AuthController::Lock, &backend);

    controller.authenticate("first");
    controller.authenticate("second");

    EXPECT_EQ(backend.authenticateCount, 1);
    EXPECT_EQ(backend.lastSecret, "first");
}

TEST(AuthControllerTest, WithoutAPasswordTheLockUnlocksOnceWithoutTheBackend)
{
    FakeBackend backend;
    AuthController controller(AuthController::Lock, &backend);
    controller.setPasswordRequired(false);
    EXPECT_FALSE(controller.passwordRequired());
    int successes = 0;
    auto succeeded = controller.authenticationSucceeded.connect([&] { ++successes; });

    controller.start();
    controller.authenticate("anything");
    controller.reset();
    EXPECT_EQ(controller.state(), AuthController::Idle);

    controller.unlockWithoutPassword();
    controller.unlockWithoutPassword();
    EXPECT_EQ(controller.state(), AuthController::Succeeded);
    EXPECT_EQ(successes, 1);
    EXPECT_EQ(backend.startCount, 0);
    EXPECT_EQ(backend.authenticateCount, 0);
    EXPECT_EQ(backend.resetCount, 0);
    EXPECT_EQ(backend.acceptCount, 0);
}

TEST(AuthControllerTest, APasswordStaysRequiredAtLoginAndWhereAsked)
{
    FakeBackend loginBackend;
    AuthController login(AuthController::Login, &loginBackend);
    login.setPasswordRequired(false);
    EXPECT_TRUE(login.passwordRequired());
    login.unlockWithoutPassword();
    EXPECT_EQ(login.state(), AuthController::Idle);
    login.authenticate("hunter2");
    EXPECT_EQ(loginBackend.authenticateCount, 1);

    FakeBackend lockBackend;
    AuthController lock(AuthController::Lock, &lockBackend);
    EXPECT_TRUE(lock.passwordRequired());
    lock.unlockWithoutPassword();
    EXPECT_EQ(lock.state(), AuthController::Idle);
    EXPECT_EQ(lockBackend.acceptCount, 0);
}

TEST(AuthControllerTest, TheLockOffersTheReaderAndAFingerUnlocksOnce)
{
    FakeBackend backend;
    backend.hasReader = true;
    backend.enrolled = true;
    AuthController controller(AuthController::Lock, &backend);
    int successes = 0;
    auto succeeded = controller.authenticationSucceeded.connect([&] { ++successes; });

    controller.start();
    EXPECT_EQ(backend.fingerprintStartCount, 1);
    EXPECT_TRUE(backend.readerRunning);

    backend.touch();
    backend.touch();
    EXPECT_EQ(controller.state(), AuthController::Succeeded);
    EXPECT_EQ(successes, 1);
    EXPECT_EQ(backend.acceptCount, 1);
}

TEST(AuthControllerTest, AFailedPasswordGivesTheReaderBackAtOnce)
{
    FakeBackend backend;
    backend.hasReader = true;
    backend.enrolled = true;
    AuthController controller(AuthController::Lock, &backend);
    controller.start();

    controller.authenticate("wrong");
    EXPECT_FALSE(backend.readerRunning);
    EXPECT_EQ(backend.fingerprintStopCount, 1);

    backend.reject();
    EXPECT_EQ(controller.state(), AuthController::Failed);
    EXPECT_TRUE(backend.readerRunning);
    EXPECT_EQ(backend.fingerprintStartCount, 2);

    controller.reset();
    EXPECT_EQ(backend.fingerprintStopCount, 1);
    EXPECT_EQ(backend.fingerprintStartCount, 2);

    backend.touch();
    EXPECT_EQ(controller.state(), AuthController::Succeeded);
}

TEST(AuthControllerTest, TheReaderIsNeverOfferedAtLoginOrWithoutOne)
{
    FakeBackend loginBackend;
    loginBackend.hasReader = true;
    loginBackend.enrolled = true;
    AuthController login(AuthController::Login, &loginBackend);
    login.start();
    login.authenticate("wrong");
    loginBackend.reject();
    EXPECT_EQ(loginBackend.fingerprintStartCount, 0);

    FakeBackend lockBackend;
    AuthController lock(AuthController::Lock, &lockBackend);
    lock.start();
    lock.authenticate("wrong");
    lockBackend.reject();
    EXPECT_EQ(lockBackend.fingerprintStartCount, 0);
}

TEST(AuthControllerTest, SomeoneAtTheLockOrAWakeOffersTheReaderAgain)
{
    FakeBackend backend;
    backend.hasReader = true;
    backend.enrolled = true;
    AuthController controller(AuthController::Lock, &backend);
    controller.start();
    EXPECT_EQ(backend.fingerprintStartCount, 1);

    controller.userActive();
    EXPECT_EQ(backend.fingerprintStartCount, 2);

    backend.wake();
    EXPECT_EQ(backend.fingerprintStartCount, 3);
    EXPECT_TRUE(backend.readerRunning);

    controller.authenticate("checking");
    controller.userActive();
    backend.wake();
    EXPECT_EQ(backend.fingerprintStartCount, 3);
    EXPECT_FALSE(backend.readerRunning);

    backend.succeed();
    controller.userActive();
    backend.wake();
    EXPECT_EQ(backend.fingerprintStartCount, 3);
}

TEST(AuthControllerTest, WithoutAnEnrolledFingerThePasswordLeads)
{
    FakeBackend backend;
    backend.hasReader = true;
    AuthController controller(AuthController::Lock, &backend);
    int changes = 0;
    auto changed = controller.fingerprintChanged.connect([&] { ++changes; });

    controller.start();
    EXPECT_EQ(backend.enrollmentCheckCount, 1);
    EXPECT_FALSE(controller.fingerprintOffered());
    EXPECT_EQ(backend.fingerprintStartCount, 0);

    backend.setEnrolled(true);
    EXPECT_EQ(changes, 1);
    EXPECT_TRUE(controller.fingerprintOffered());
    EXPECT_TRUE(backend.readerRunning);

    backend.setEnrolled(false);
    EXPECT_EQ(changes, 2);
    EXPECT_FALSE(controller.fingerprintOffered());
    EXPECT_FALSE(backend.readerRunning);

    FakeBackend loginBackend;
    loginBackend.hasReader = true;
    loginBackend.enrolled = true;
    AuthController login(AuthController::Login, &loginBackend);
    login.start();
    EXPECT_EQ(loginBackend.enrollmentCheckCount, 0);
    EXPECT_FALSE(login.fingerprintOffered());
}

TEST(AuthControllerTest, AMisreadFingerAsksForThePasswordAndFiveEndTheReader)
{
    FakeBackend backend;
    backend.hasReader = true;
    backend.enrolled = true;
    AuthController controller(AuthController::Lock, &backend);
    int asked = 0;
    auto askedForPassword = controller.passwordAsked.connect([&] { ++asked; });
    controller.start();

    backend.mismatch();
    EXPECT_EQ(asked, 1);
    EXPECT_FALSE(controller.fingerprintLockedOut());
    backend.mismatch();
    backend.mismatch();
    backend.mismatch();
    EXPECT_EQ(asked, 4);
    EXPECT_FALSE(controller.fingerprintLockedOut());

    const int stops = backend.fingerprintStopCount;
    backend.mismatch();
    EXPECT_EQ(asked, 5);
    EXPECT_TRUE(controller.fingerprintLockedOut());
    EXPECT_EQ(backend.fingerprintStopCount, stops + 1);
    EXPECT_TRUE(controller.fingerprintOffered());

    const int starts = backend.fingerprintStartCount;
    controller.userActive();
    backend.wake();
    EXPECT_EQ(backend.fingerprintStartCount, starts);
    backend.touch();
    EXPECT_NE(controller.state(), AuthController::Succeeded);

    controller.authenticate("right");
    backend.succeed();
    EXPECT_EQ(controller.state(), AuthController::Succeeded);
}
