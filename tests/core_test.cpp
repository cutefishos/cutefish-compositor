// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include "base/paths.h"
#include "core/compositor_options.h"
#include "core/logging.h"
#include "core/preferences.h"
#include "core/process_environment.h"

namespace cutefish {
namespace {

std::optional<core::CompositorOptions> parse(std::vector<std::string> arguments)
{
    arguments.insert(arguments.begin(), "cutefish-compositor");
    std::vector<char *> argv;
    for (std::string &argument : arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    return core::CompositorOptions::parse(int(arguments.size()), argv.data());
}

TEST(CompositorOptionsTest, Defaults)
{
    const auto options = parse({});
    ASSERT_TRUE(options);
    EXPECT_EQ(options->socketName, "");
    EXPECT_FALSE(options->noDrm);
    EXPECT_EQ(options->headlessOutputs, 1);
    EXPECT_EQ(options->readyFd, -1);
    EXPECT_EQ(options->scale.numerator(), 120u);
    EXPECT_EQ(options->displayConfigService, "org.cutefish.DisplayConfig");
    EXPECT_TRUE(options->clipboardClients.empty());
    EXPECT_TRUE(options->logRules.empty());
}

TEST(CompositorOptionsTest, ReadsValuesAndRepeats)
{
    const auto options = parse({"--socket", "wayland-9", "--no-drm", "--headless-outputs=3",
                                "--scale", "1.5", "--capture-client", "/a", "--capture-client=/b",
                                "--login", "--input-method", "fcitx5", "--log-rules",
                                "cutefish.drm.debug=true"});
    ASSERT_TRUE(options);
    EXPECT_EQ(options->socketName, "wayland-9");
    EXPECT_TRUE(options->noDrm);
    EXPECT_EQ(options->headlessOutputs, 3);
    EXPECT_EQ(options->scale.numerator(), 180u);
    EXPECT_EQ(options->captureClients, (std::vector<std::string> {"/a", "/b"}));
    EXPECT_TRUE(options->login);
    EXPECT_EQ(options->inputMethodId, "fcitx5");
    EXPECT_EQ(options->logRules, "cutefish.drm.debug=true");
}

TEST(CompositorOptionsTest, OnlyThePortalBackendCapturesByItsPath)
{
    const auto defaults = parse({});
    ASSERT_TRUE(defaults);
    EXPECT_EQ(core::captureExecutables(*defaults), (std::vector<std::string> {CUTEFISH_PORTAL_BACKEND}));
    const auto added = parse({"--capture-client", "/a"});
    ASSERT_TRUE(added);
    const std::vector<std::string> executables = core::captureExecutables(*added);
    EXPECT_EQ(executables, (std::vector<std::string> {CUTEFISH_PORTAL_BACKEND, "/a"}));
    for (const std::string &tool : {std::string(CUTEFISH_BINDIR "/cutefish-screenshot"),
                                    std::string(CUTEFISH_SCREEN_RECORDER)})
        EXPECT_EQ(std::find(executables.begin(), executables.end(), tool), executables.end()) << tool;
}

TEST(CompositorOptionsTest, RejectsInvalidValues)
{
    EXPECT_FALSE(parse({"--scale", "5"}));
    EXPECT_FALSE(parse({"--scale", "1,5"}));
    EXPECT_FALSE(parse({"--no-drm", "--headless-outputs", "0"}));
    EXPECT_FALSE(parse({"--no-drm", "--headless-outputs", "17"}));
    EXPECT_FALSE(parse({"--ready-fd", "2"}));
    EXPECT_FALSE(parse({"--ready-fd", "x"}));
    EXPECT_TRUE(parse({"--headless-outputs", "0"}));
}

TEST(CompositorOptionsTest, TakesTheReadyDescriptor)
{
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    const int fd = fds[1] >= 3 ? fds[1] : -1;
    ASSERT_GE(fd, 3);
    const auto options = parse({"--ready-fd", std::to_string(fd)});
    ASSERT_TRUE(options);
    EXPECT_EQ(options->readyFd, fd);
    close(fds[0]);
    close(fds[1]);
}

TEST(LoggingTest, RulesReachTheCategories)
{
    EXPECT_TRUE(cfDrm.isEnabled(base::LogLevel::Info));
    EXPECT_FALSE(cfDrm.isEnabled(base::LogLevel::Debug));

    core::setLoggingRules("cutefish.drm.debug=true;cutefish.scene=false");
    EXPECT_TRUE(cfDrm.isEnabled(base::LogLevel::Debug));
    EXPECT_FALSE(cfScene.isEnabled(base::LogLevel::Warning));

    core::setLoggingRules("");
    EXPECT_FALSE(cfDrm.isEnabled(base::LogLevel::Debug));
    EXPECT_TRUE(cfScene.isEnabled(base::LogLevel::Warning));
}

TEST(LoggingTest, TheMacroWritesOneLine)
{
    std::vector<std::string> lines;
    base::logging::setHandler([&](const base::LogCategory &category, base::LogLevel level,
                                  std::string_view message) {
        lines.push_back(base::logging::formatLine(category, level, message));
    });
    CF_INFO(cfWayland) << "started" << 3;
    base::logging::setHandler(nullptr);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines.front(), "cutefish.wayland: started 3");
}

TEST(PreferencesTest, TestsWriteUnderTheirOwnConfigHome)
{
    const std::string scratch = std::filesystem::path(preferencesDirectory()).parent_path();
    EXPECT_NE(scratch, base::paths::configHome());

    setConfigHomeForTesting("/tmp/cutefish-core-test");
    EXPECT_EQ(preferencesDirectory(), "/tmp/cutefish-core-test/cutefish");
    EXPECT_EQ(preferencesFile("compositor"), "/tmp/cutefish-core-test/cutefish/compositor.conf");

    setConfigHomeForTesting({});
    EXPECT_EQ(preferencesFile("shell"), base::paths::configHome() + "/cutefish/shell.conf");
    setConfigHomeForTesting(scratch);
}

TEST(ProcessEnvironmentTest, ChildrenGetTheCurrentEnvironment)
{
    setenv("CUTEFISH_CORE_TEST_SET", "outer", 1);
    unsetenv("CUTEFISH_CORE_TEST_UNSET");

    const std::vector<std::string> child = childEnvironment();
    EXPECT_EQ(std::count(child.begin(), child.end(), "CUTEFISH_CORE_TEST_SET=outer"), 1);
    EXPECT_EQ(std::count_if(child.begin(), child.end(), [](const std::string &entry) {
                  return entry.starts_with("CUTEFISH_CORE_TEST_UNSET=");
              }),
              0);
    EXPECT_GT(child.size(), 1u);
}

} // namespace
} // namespace cutefish
