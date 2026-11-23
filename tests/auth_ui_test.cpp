// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/geometry.h"
#include "auth/auth_controller.h"
#include "auth/auth_status.h"
#include "auth/auth_ui.h"
#include "auth/auth_view.h"
#include "auth/login_sessions.h"
#include "compositor/compositor.h"
#include "core/main_loop.h"
#include "core/preferences.h"
#include "cutefish/networkmanager/client.h"
#include "fake_backend.h"
#include "fake_renderer.h"
#include "input/input_manager.h"
#include "input/seat.h"
#include "scene/node.h"
#include "session/secure_container.h"
#include "session/session_controller.h"
#include "shell/menubar/status_icon.h"
#include "base/settings.h"
#include "loop_test_util.h"
#include "temporary_directory.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <linux/input-event-codes.h>
#include <memory>
#include <optional>
#include <sstream>
#include <sys/stat.h>
#include <tuple>

#include <gtest/gtest.h>

#include "toolkit/toolkit_host.h"
#include "ui/application.h"
#include "ui/bitmap.h"
#include "ui/embedded_window.h"
#include "ui/label.h"
#include "ui/text_field.h"
#include "ui/view.h"
using namespace cutefish;

namespace {

ui::TextField *focusedField(const auth::AuthUi &ui)
{
    return dynamic_cast<ui::TextField *>(ui.focusView());
}

std::string fieldText(const auth::AuthUi &ui)
{
    const ui::TextField *field = focusedField(ui);
    return field ? field->text() : std::string();
}

// Pixels of renderImage() in premultiplied 0xAARRGGBB.
template<typename Image>
bool isPainted(const Image &image, int x, int y)
{
    return (image.pixel(x, y) >> 24) != 0;
}

template<typename Image>
base::Size sizeOf(const Image &image)
{
    return base::Size(image.width(), image.height());
}

ui::View *findView(ui::View *view, const std::string &name)
{
    if (!view || view->name() == name)
        return view;
    for (const auto &child : view->children()) {
        if (ui::View *found = findView(child.get(), name))
            return found;
    }
    return nullptr;
}

// The view's rect in panel coordinates.
base::RectF panelRect(const ui::View *view)
{
    const ui::PointF origin = view->mapToWindow({0, 0});
    return base::RectF(origin.x, origin.y, view->geometry().width, view->geometry().height);
}

using Rgba = std::array<uint8_t, 4>;

constexpr Rgba kRed {255, 0, 0, 255};
constexpr Rgba kGreen {0, 255, 0, 255};
constexpr Rgba kBlue {0, 0, 255, 255};

Rgba pixelAt(const testsupport::FakeTexture *texture, int x, int y)
{
    const uint8_t *pixel =
            texture->pixels().data() + (size_t(y) * size_t(texture->size().width()) + x) * 4;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

bool savePng(const std::string &path, uint32_t left, uint32_t right = 0, int split = 80)
{
    ui::Bitmap image(80, 40, ui::Bitmap::Format::Argb32Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, x < split ? left : right);
    }
    return image.savePng(path);
}

struct ConfigurationHome {
    test::TemporaryDirectory directory;
    ConfigurationHome() { setConfigHomeForTesting(directory.path()); }
};
const ConfigurationHome configurationHome;

void withoutNetworkManager()
{
    networkmanager::Client::setInstanceBus(base::dbus::Connection::session(core::mainLoop()));
}

std::vector<std::string> translationsOf(const std::string &msgid)
{
    std::vector<std::string> translations;
    const std::string key = "msgid \"" + msgid + "\"\nmsgstr \"";
    for (const auto &entry : std::filesystem::directory_iterator(CUTEFISH_PO_DIR)) {
        if (entry.path().extension() != ".po")
            continue;
        std::ifstream file(entry.path());
        std::stringstream contents;
        contents << file.rdbuf();
        const std::string text = contents.str();
        const size_t found = text.find(key);
        if (found == std::string::npos)
            continue;
        const size_t start = found + key.size();
        translations.push_back(text.substr(start, text.find('"', start) - start));
    }
    return translations;
}

} // namespace


TEST(AuthUiTest, WallpaperIsSharpPerOutputAndFailureKeepsShield)
{
    test::TemporaryDirectory files;
    ASSERT_TRUE(files.isValid());
    const std::string path = files.filePath("wallpaper.png");
    ASSERT_TRUE(savePng(path, 0xffff0000, 0xff0000ff, 40));
    base::Settings settings(preferencesFile("services"));
    settings.setValue("Appearance/Wallpaper", path);
    settings.sync();
    scene::Node root;
    session::SecureContainer secure(&root);
    secure.setOutputs({{"one", base::Rect(0, 0, 640, 320)},
                       {"two", base::Rect(640, 0, 320, 640)}});
    secure.setBlocked(true);
    testsupport::FakeRenderer renderer;
    const auto rendered = [&] {
        renderer.calls.clear();
        root.render(renderer);
        return renderer.calls.size();
    };
    EXPECT_TRUE(test::runUntil([&] { return rendered() == 4; }));
    EXPECT_EQ(renderer.calls.at(0).kind, "fill");
    EXPECT_EQ(renderer.calls.at(0).color.a, 1.0f);
    const auto *texture = static_cast<testsupport::FakeTexture *>(renderer.calls.at(1).texture);
    EXPECT_EQ(texture->size(), base::Size(640, 320));
    EXPECT_EQ(pixelAt(texture, 0, 160), kRed);
    EXPECT_EQ(pixelAt(texture, 300, 160), kRed);
    EXPECT_EQ(pixelAt(texture, 340, 160), kBlue);
    EXPECT_EQ(renderer.calls.at(3).texture->size(), base::Size(320, 640));
    secure.setBlocked(false);
    settings.setValue("Appearance/Wallpaper", files.filePath("missing"));
    settings.sync();
    secure.setBlocked(true);
    EXPECT_EQ(rendered(), 2);
    for (const auto &call : renderer.calls) {
        EXPECT_EQ(call.kind, "fill");
        EXPECT_EQ(call.color.a, 1.0f);
        EXPECT_EQ(call.color.r, 0.0f);
    }
    std::filesystem::remove(settings.path());
}

TEST(AuthUiTest, PrewarmedUiRepeatsKeysAndUpdatesCursor)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    scene::Node applications;
    compositor::Compositor compositor(&applications);
    input::Seat seat(&compositor);
    seat.setSessionGate(&controller);
    input::InputManager input(&seat);
    FakeBackend backend;
    auth::AuthUi ui(&controller, seat.xkbTracker(), &backend);
    ui.refresh();
    auto *window = ui.window();
    ASSERT_TRUE(window);
    EXPECT_TRUE(!root.isVisible());
    EXPECT_TRUE(!ui.renderImage().isNull());
    EXPECT_EQ(backend.startCount, 0);
    EXPECT_TRUE(controller.requestLock());
    ui.refresh();
    EXPECT_EQ(ui.window(), window);
    EXPECT_EQ(backend.startCount, 1);
    auto *field = focusedField(ui);
    ASSERT_TRUE(field);
    field->setText("abcdefghij");
    input.onKey({}, KEY_BACKSPACE, true);
    EXPECT_EQ(fieldText(ui).size(), 9u);
    EXPECT_TRUE(test::runUntil([&] { return fieldText(ui).size() < 9; },
                               std::chrono::milliseconds(1000)));
    input.onKey({}, KEY_BACKSPACE, false);
    const std::string remaining = fieldText(ui);
    test::runFor(std::chrono::milliseconds(120));
    EXPECT_EQ(fieldText(ui), remaining);
    std::vector<input::CursorShape> cursors;
    auto cursorSubscription = ui.cursorShapeChanged.connect(
            [&](input::CursorShape shape) { cursors.push_back(shape); });
    const base::PointF fieldCenter = ui.fieldBounds().center()
            + base::PointF(secure.authContainer()->children().front()->absolutePosition());
    ui.onPointerMotion({}, fieldCenter);
    EXPECT_TRUE(!cursors.empty());
    EXPECT_EQ(cursors.back(), input::CursorShape::Text);
    ui.onPointerMotion({}, base::PointF(0, 0));
    EXPECT_EQ(cursors.back(), input::CursorShape::Default);
    input.onKey({}, KEY_BACKSPACE, true);
    controller.notifyFramePresented();
    EXPECT_TRUE(controller.requestUnlock());
    const std::string beforeUnlock = fieldText(ui);
    test::runFor(std::chrono::milliseconds(600));
    EXPECT_EQ(fieldText(ui), beforeUnlock);
    input.onKey({}, KEY_BACKSPACE, false);
    seat.shutdown();
}

TEST(AuthUiTest, WallpaperFollowsSettingsWhileUnlocked)
{
    test::TemporaryDirectory files;
    ASSERT_TRUE(files.isValid());
    const auto save = [&](const std::string &name, uint32_t color) {
        const std::string path = files.filePath(name);
        return savePng(path, color) ? path : std::string();
    };
    base::Settings settings(preferencesFile("services"));
    settings.setValue("Appearance/Wallpaper", save("red.png", 0xffff0000));
    settings.sync();
    scene::Node root;
    session::SecureContainer secure(&root);
    secure.setOutputs({{"one", base::Rect(0, 0, 640, 320)}});
    testsupport::FakeRenderer renderer;
    const auto shown = [&] {
        renderer.calls.clear();
        secure.shieldContainer()->render(renderer);
        for (const auto &call : renderer.calls) {
            if (const auto *texture = static_cast<testsupport::FakeTexture *>(call.texture))
                return std::optional<Rgba>(pixelAt(texture, 0, 0));
        }
        return std::optional<Rgba>();
    };
    EXPECT_TRUE(test::runUntil([&] { return shown() == kRed; }));
    settings.setValue("Appearance/Wallpaper", save("blue.png", 0xff0000ff));
    settings.sync();
    EXPECT_TRUE(test::runUntil([&] { return shown() == kBlue; }));
    struct stat blue;
    ASSERT_EQ(0, stat(files.filePath("blue.png").c_str(), &blue));
    save("blue.png", 0xff00ff00);
    const struct timespec times[2] = {blue.st_atim, blue.st_mtim};
    ASSERT_EQ(0, utimensat(AT_FDCWD, files.filePath("blue.png").c_str(), times, 0));
    EXPECT_TRUE(test::runUntil([&] { return shown() == kGreen; }));
    std::filesystem::remove(settings.path());
}

TEST(AuthUiTest, PreparedPanelIdlesUntilTheNextLock)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    EXPECT_TRUE(ui.isRefreshPending());
    ui.refresh();
    EXPECT_TRUE(ui.window());
    EXPECT_TRUE(!ui.renderImage().isNull());
    EXPECT_TRUE(!ui.window()->hasKeyboardFocus());
    test::runFor(std::chrono::milliseconds(100));
    EXPECT_TRUE(!ui.isRefreshPending());
    EXPECT_TRUE(controller.requestLock());
    EXPECT_TRUE(ui.isRefreshPending());
    test::runFor(std::chrono::milliseconds(100));
    EXPECT_TRUE(!ui.isRefreshPending());
    EXPECT_TRUE(ui.window()->hasKeyboardFocus());
    EXPECT_TRUE(focusedField(ui));
    ui.controller()->authenticate("valid");
    backend.succeed();
    controller.notifyFramePresented();
    EXPECT_TRUE(test::runUntil(
            [&] { return controller.state() == session::SessionState::Unlocking; }));
    controller.notifyFramePresented();
    EXPECT_EQ(controller.state(), session::SessionState::Active);
    // Rendering the panel takes longer than 100 ms under ASan.
    EXPECT_TRUE(test::runUntil([&] { return !ui.isRefreshPending(); },
                               std::chrono::milliseconds(5000)));
    test::runFor(std::chrono::milliseconds(100));
    EXPECT_TRUE(!ui.isRefreshPending());
    EXPECT_TRUE(ui.isPanelDrawn());
    EXPECT_TRUE(ui.window());
    EXPECT_TRUE(!ui.window()->hasKeyboardFocus());
    EXPECT_EQ(ui.controller()->state(), auth::AuthController::Idle);
}

TEST(AuthUiTest, PanelFollowsOutputChanges)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    test::runFor(std::chrono::milliseconds(50));
    EXPECT_TRUE(!ui.isPanelDrawn());
    EXPECT_TRUE(!ui.isRefreshPending());
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    EXPECT_TRUE(test::runUntil([&] { return ui.isPanelDrawn(); }));
    const base::Size size = sizeOf(ui.renderImage());
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600), 2}});
    EXPECT_TRUE(test::runUntil([&] {
        return sizeOf(ui.renderImage()) == base::Size(size.width() * 2, size.height() * 2);
    }));
}

TEST(AuthUiTest, OffscreenRenderingAndDirectInput)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    scene::Node applications;
    compositor::Compositor compositor(&applications);
    input::Seat seat(&compositor);
    seat.setSessionGate(&controller);
    input::InputManager input(&seat);
    FakeBackend backend;
    auth::AuthUi ui(&controller, seat.xkbTracker(), &backend);
    EXPECT_TRUE(controller.requestLock());
    controller.notifyFramePresented();
    ui.refresh();
    EXPECT_TRUE(ui.window());
    const auto image = ui.renderImage();
    EXPECT_TRUE(!image.isNull());
    bool painted = false;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            painted |= isPainted(image, x, y);
    EXPECT_TRUE(painted);
    input.onKey({}, KEY_A, true);
    input.onKey({}, KEY_A, false);
    input.onKey({}, KEY_ENTER, true);
    input.onKey({}, KEY_ENTER, false);
    EXPECT_EQ(backend.authenticateCount, 1);
    EXPECT_EQ(backend.lastSecret, std::string("a"));
    backend.reject();
    EXPECT_EQ(controller.state(), session::SessionState::Locked);
    backend.succeed();
    EXPECT_EQ(controller.state(), session::SessionState::Locked);
    ui.controller()->authenticate("valid");
    backend.succeed();
    backend.succeed();
    EXPECT_EQ(backend.acceptCount, 1);
    EXPECT_EQ(controller.state(), session::SessionState::Unlocking);
    EXPECT_TRUE(root.isVisible());
    controller.notifyFramePresented();
    EXPECT_EQ(controller.state(), session::SessionState::Active);
    EXPECT_TRUE(controller.requestLock());
    controller.notifyFramePresented();
    ui.refresh();
    EXPECT_EQ(ui.controller()->state(), auth::AuthController::Idle);
    seat.shutdown();
}

TEST(AuthUiTest, SubmittedPasswordStaysUntilRejected)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    scene::Node applications;
    compositor::Compositor compositor(&applications);
    input::Seat seat(&compositor);
    seat.setSessionGate(&controller);
    input::InputManager input(&seat);
    FakeBackend backend;
    auth::AuthUi ui(&controller, seat.xkbTracker(), &backend);
    EXPECT_TRUE(controller.requestLock());
    controller.notifyFramePresented();
    ui.refresh();
    auto *field = focusedField(ui);
    ASSERT_TRUE(field);
    const auto submit = [&](int key) {
        input.onKey({}, key, true);
        input.onKey({}, key, false);
        input.onKey({}, KEY_ENTER, true);
        input.onKey({}, KEY_ENTER, false);
    };
    submit(KEY_A);
    EXPECT_EQ(backend.authenticateCount, 1);
    EXPECT_EQ(field->text(), std::string("a"));
    EXPECT_TRUE(!field->isEnabled());
    backend.reject();
    EXPECT_EQ(field->text(), std::string());
    EXPECT_TRUE(field->isEnabled());
    EXPECT_TRUE(field->hasFocus());
    submit(KEY_B);
    EXPECT_EQ(backend.lastSecret, std::string("b"));
    backend.succeed();
    EXPECT_EQ(controller.state(), session::SessionState::Unlocking);
    EXPECT_EQ(field->text(), std::string("b"));
    EXPECT_TRUE(!field->isEnabled());
    seat.shutdown();
}

TEST(AuthUiTest, WithoutAPasswordEnterUnlocksAndTheBackendIsNeverAsked)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    scene::Node applications;
    compositor::Compositor compositor(&applications);
    input::Seat seat(&compositor);
    seat.setSessionGate(&controller);
    input::InputManager input(&seat);
    FakeBackend backend;
    auth::AuthUi ui(&controller, seat.xkbTracker(), &backend);
    ui.setPasswordRequired(false);
    const auto press = [&](int key) {
        input.onKey({}, key, true);
        input.onKey({}, key, false);
    };

    for (int enter : {KEY_ENTER, KEY_KPENTER}) {
        ASSERT_TRUE(controller.requestLock());
        controller.notifyFramePresented();
        ui.refresh();
        ASSERT_EQ(controller.state(), session::SessionState::Locked);
        ui::View *screen = ui.window()->contentView();
        ui::View *prompt = findView(screen, "unlock-prompt");
        ASSERT_TRUE(prompt);
        EXPECT_TRUE(prompt->isVisible());
        EXPECT_FALSE(findView(screen, "password-field")->isVisible());
        EXPECT_EQ(ui.focusView(), prompt);
        EXPECT_TRUE(ui.fieldBounds().isEmpty());

        press(KEY_A);
        press(KEY_SPACE);
        press(KEY_ESC);
        press(KEY_TAB);
        EXPECT_EQ(controller.state(), session::SessionState::Locked);
        EXPECT_EQ(ui.focusView(), prompt);

        press(enter);
        EXPECT_EQ(controller.state(), session::SessionState::Unlocking);
        controller.notifyFramePresented();
        EXPECT_EQ(controller.state(), session::SessionState::Active);
        ui.refresh();
    }
    EXPECT_EQ(backend.startCount, 0);
    EXPECT_EQ(backend.authenticateCount, 0);
    EXPECT_EQ(backend.resetCount, 0);
    EXPECT_EQ(backend.acceptCount, 0);
    seat.shutdown();
}

TEST(AuthUiTest, WithoutAPasswordAClickWhereTheFieldWasDoesNotUnlock)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    ui.setPasswordRequired(false);
    controller.requestLock();
    controller.notifyFramePresented();
    ui.refresh();
    ui.window()->updateLayout();
    ui::View *prompt = findView(ui.window()->contentView(), "unlock-prompt");
    ASSERT_TRUE(prompt);
    const base::PointF centre = panelRect(prompt).center() + base::PointF(160, 0);
    ui.onPointerButton({}, centre, BTN_LEFT, true);
    ui.onPointerButton({}, centre, BTN_LEFT, false);
    EXPECT_EQ(controller.state(), session::SessionState::Locked);
    EXPECT_EQ(ui.focusView(), prompt);
    EXPECT_EQ(backend.authenticateCount, 0);
}

TEST(AuthUiTest, TheUnlockPromptSitsWhereThePasswordFieldWas)
{
    for (double scale : {1.0, 2.0}) {
        scene::Node root;
        session::SecureContainer secure(&root);
        session::SessionController controller(&secure);
        controller.setOutputs({{"one", base::Rect(0, 0, 800, 600), scale}});

        FakeBackend withBackend;
        auth::AuthUi withPassword(&controller, nullptr, &withBackend);
        withPassword.refresh();
        ASSERT_FALSE(withPassword.renderImage().isNull());
        const base::RectF field = withPassword.fieldBounds();
        ASSERT_FALSE(field.isEmpty());

        FakeBackend withoutBackend;
        auth::AuthUi withoutPassword(&controller, nullptr, &withoutBackend);
        withoutPassword.setPasswordRequired(false);
        withoutPassword.refresh();
        const auto image = withoutPassword.renderImage();
        ASSERT_FALSE(image.isNull());
        ui::View *prompt = findView(withoutPassword.window()->contentView(), "unlock-prompt");
        ASSERT_TRUE(prompt);
        const base::RectF box = panelRect(prompt);
        EXPECT_EQ(box, field);

        double weight = 0;
        int left = image.width(), right = -1, top = image.height(), bottom = -1;
        for (int y = int(std::floor(field.y() * scale)); y < int(std::ceil(field.bottom() * scale)); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const double red = double((image.pixel(x, y) >> 16) & 0xff) / 255;
                if (red <= 0)
                    continue;
                weight += red;
                if (red > 0.5) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        }
        ASSERT_GT(weight, 0);
        const double fieldX = field.center().x() * scale;
        const double fieldY = field.center().y() * scale;
        const double inkX = (left + right + 1) / 2.0;
        const double inkY = (top + bottom + 1) / 2.0;
        EXPECT_LE(std::abs(inkX - fieldX), 0.5 * scale + 0.5);
        EXPECT_LE(std::abs(inkY - fieldY), 0.5 * scale + 0.5);
    }
}

TEST(AuthUiTest, TheUnlockPromptFitsInEveryLanguage)
{
    const std::vector<std::string> texts = translationsOf("Press Enter to unlock");
    ASSERT_EQ(texts.size(), 16u);
    for (double scale : {1.0, 2.0}) {
        scene::Node root;
        session::SecureContainer secure(&root);
        session::SessionController controller(&secure);
        controller.setOutputs({{"one", base::Rect(0, 0, 800, 600), scale}});
        FakeBackend backend;
        auth::AuthUi ui(&controller, nullptr, &backend);
        ui.setPasswordRequired(false);
        ui.refresh();
        ui::View *prompt = findView(ui.window()->contentView(), "unlock-prompt");
        ASSERT_TRUE(prompt && !prompt->children().empty());
        auto *label = dynamic_cast<ui::Label *>(prompt->children().front().get());
        ASSERT_TRUE(label);
        for (const std::string &text : texts) {
            label->setText(text);
            ui.window()->updateLayout();
            EXPECT_LE(label->sizeHint().width, label->geometry().width) << text << " at " << scale;
            EXPECT_NEAR(label->geometry().x + label->geometry().width / 2, prompt->geometry().width / 2, 0.5)
                    << text << " at " << scale;
        }
    }
}

TEST(AuthUiTest, TheLoginScreenAsksForAPasswordWhateverTheLockDoes)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure, nullptr, session::SessionState::Login);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    ui.setPasswordRequired(false);
    ui.refresh();
    ui::View *screen = ui.window()->contentView();
    EXPECT_TRUE(findView(screen, "password-field")->isVisible());
    EXPECT_FALSE(findView(screen, "unlock-prompt")->isVisible());
    EXPECT_TRUE(focusedField(ui));
    EXPECT_EQ(backend.startCount, 1);
    ui.controller()->unlockWithoutPassword();
    EXPECT_EQ(ui.controller()->state(), auth::AuthController::Idle);
    ui.controller()->authenticate("valid");
    EXPECT_EQ(backend.authenticateCount, 1);
    backend.succeed();
    EXPECT_EQ(backend.acceptCount, 1);
    EXPECT_EQ(controller.state(), session::SessionState::Login);
}

TEST(AuthUiTest, PanelIsDrawnAtTheOutputDensity)
{
    const auto paintedArea = [](double scale) {
        scene::Node root;
        session::SecureContainer secure(&root);
        session::SessionController controller(&secure);
        controller.setOutputs({{"one", base::Rect(0, 0, 800, 600), scale}});
        FakeBackend backend;
        auth::AuthUi ui(&controller, nullptr, &backend);
        ui.refresh();
        const auto image = ui.renderImage();
        base::Rect painted;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (isPainted(image, x, y))
                    painted |= base::Rect(x, y, 1, 1);
        return std::tuple(sizeOf(image), painted);
    };
    const auto [plainSize, plain] = paintedArea(1);
    const auto [denseSize, dense] = paintedArea(1.5);
    EXPECT_EQ(plainSize, base::Size(480, 600));
    EXPECT_EQ(denseSize, base::Size(720, 900));
    EXPECT_TRUE(!plain.isEmpty());
    EXPECT_TRUE(std::abs(dense.width() - plain.width() * 1.5) <= 2);
    EXPECT_TRUE(std::abs(dense.height() - plain.height() * 1.5) <= 2);
    EXPECT_TRUE(std::abs(dense.left() - plain.left() * 1.5) <= 2);
}

TEST(AuthUiTest, ChoiceMenuClosesOnAPressElsewhere)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure, nullptr, session::SessionState::Login);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    ui.refresh();
    ui.controller()->setSessions({{"cutefish-wayland", "Cutefish", {}, {}},
                                  {"plasma", "Plasma", {}, {}}});
    ui.window()->updateLayout();
    ui::View *screen = ui.window()->contentView();
    ui::View *button = findView(screen, "session-menu-button");
    ui::View *list = findView(screen, "session-menu-list");
    EXPECT_TRUE(button && list);
    EXPECT_TRUE(!list->isVisible());
    const base::PointF panel(secure.authContainer()->children().front()->absolutePosition());
    const auto click = [&](base::PointF point) {
        ui.onPointerMotion({}, panel + point);
        ui.onPointerButton({}, panel + point, BTN_LEFT, true);
        ui.onPointerButton({}, panel + point, BTN_LEFT, false);
    };
    const base::PointF onButton = panelRect(button).center();
    click(onButton);
    EXPECT_TRUE(list->isVisible());
    ui.window()->updateLayout();
    EXPECT_TRUE(panelRect(list).bottom() <= panelRect(button).top());
    click({20, 20});
    EXPECT_TRUE(!list->isVisible());
    click(onButton);
    EXPECT_TRUE(list->isVisible());
    click(onButton);
    EXPECT_TRUE(!list->isVisible());
    click(onButton);
    ui.window()->updateLayout();
    const base::RectF rows = panelRect(list);
    click(base::PointF(rows.center().x(), rows.top() + 6 + 30 + 15));
    EXPECT_TRUE(!list->isVisible());
    EXPECT_EQ(ui.controller()->sessionIndex(), 1);
}

class UserGridTest : public ::testing::Test
{
protected:
    UserGridTest()
        : secure(&root)
        , controller(&secure, nullptr, session::SessionState::Login)
        , compositor(&applications)
        , seat(&compositor)
        , input(&seat)
    {
        controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
        seat.setSessionGate(&controller);
        ui = std::make_unique<auth::AuthUi>(&controller, seat.xkbTracker(), &backend);
        ui->refresh();
        ui->controller()->setUsers({"tester", "alice", "bob"});
        ui->window()->updateLayout();
        screen = ui->window()->contentView();
        avatar = findView(screen, "user-avatar");
        grid = findView(screen, "user-grid");
        panel = base::PointF(secure.authContainer()->children().front()->absolutePosition());
    }

    ~UserGridTest() override { seat.shutdown(); }

    void click(base::PointF point)
    {
        ui->onPointerMotion({}, panel + point);
        ui->onPointerButton({}, panel + point, BTN_LEFT, true);
        ui->onPointerButton({}, panel + point, BTN_LEFT, false);
    }

    void press(uint32_t key)
    {
        input.onKey({}, key, true);
        input.onKey({}, key, false);
    }

    base::RectF rectOf(const std::string &name)
    {
        ui->window()->updateLayout();
        return panelRect(findView(screen, name));
    }

    bool settled(bool open)
    {
        ui->renderImage();
        return open ? ui->fieldBounds().isEmpty() && ui->fieldOpacity() == 0
                    : !grid->isVisible() && ui->fieldOpacity() == 1;
    }

    scene::Node root;
    session::SecureContainer secure;
    session::SessionController controller;
    scene::Node applications;
    compositor::Compositor compositor;
    input::Seat seat;
    input::InputManager input;
    FakeBackend backend;
    std::unique_ptr<auth::AuthUi> ui;
    ui::View *screen = nullptr;
    ui::View *avatar = nullptr;
    ui::View *grid = nullptr;
    base::PointF panel;
};

TEST_F(UserGridTest, TheAvatarOpensTheAccountsAndATilePicksOne)
{
    ASSERT_TRUE(avatar && grid);
    EXPECT_TRUE(avatar->acceptsPointer());
    EXPECT_TRUE(!grid->isVisible());
    EXPECT_EQ(findView(screen, "user-menu"), nullptr);

    click(rectOf("user-avatar").center());
    EXPECT_TRUE(grid->isVisible());
    EXPECT_EQ(ui->focusView(), grid);
    EXPECT_TRUE(test::runUntil([&] { return settled(true); }, std::chrono::milliseconds(1000)));

    const base::RectF first = rectOf("user-tile-0");
    const base::RectF last = rectOf("user-tile-2");
    EXPECT_EQ(first.top(), last.top());
    EXPECT_NEAR(first.left() + last.right(), 480, 1e-6);
    EXPECT_NEAR(first.top() + 6, rectOf("user-avatar").top(), 1e-6);

    click(rectOf("user-tile-2").center());
    EXPECT_EQ(ui->controller()->userName(), "bob");
    EXPECT_TRUE(test::runUntil([&] { return settled(false); }, std::chrono::milliseconds(1000)));
    EXPECT_TRUE(focusedField(*ui));
    EXPECT_TRUE(!ui->fieldBounds().isEmpty());
}

TEST_F(UserGridTest, EscapeAndAPressElsewhereGoBack)
{
    click(rectOf("user-avatar").center());
    EXPECT_TRUE(test::runUntil([&] { return settled(true); }, std::chrono::milliseconds(1000)));
    press(KEY_ESC);
    EXPECT_TRUE(test::runUntil([&] { return settled(false); }, std::chrono::milliseconds(1000)));
    EXPECT_TRUE(focusedField(*ui));
    EXPECT_EQ(ui->controller()->userName(), "tester");

    click(rectOf("user-avatar").center());
    EXPECT_TRUE(test::runUntil([&] { return settled(true); }, std::chrono::milliseconds(1000)));
    click({20, 20});
    EXPECT_TRUE(test::runUntil([&] { return settled(false); }, std::chrono::milliseconds(1000)));
    EXPECT_EQ(ui->controller()->userName(), "tester");
}

TEST_F(UserGridTest, ArrowsAndReturnPickFromTheKeyboard)
{
    click(rectOf("user-avatar").center());
    press(KEY_RIGHT);
    press(KEY_ENTER);
    EXPECT_EQ(ui->controller()->userName(), "alice");
    EXPECT_TRUE(test::runUntil([&] { return settled(false); }, std::chrono::milliseconds(1000)));

    click(rectOf("user-avatar").center());
    press(KEY_ENTER);
    EXPECT_EQ(ui->controller()->userName(), "alice");
}

TEST_F(UserGridTest, NothingToSwitchToWithOneAccountOrWhileChecking)
{
    ui->controller()->setUsers({"tester"});
    EXPECT_TRUE(!avatar->acceptsPointer());
    click(rectOf("user-avatar").center());
    EXPECT_TRUE(!grid->isVisible());

    ui->controller()->setUsers({"tester", "alice"});
    click(rectOf("user-avatar").center());
    EXPECT_TRUE(grid->isVisible());
    press(KEY_ESC);
    for (uint32_t key : {KEY_P, KEY_W, KEY_ENTER})
        press(key);
    EXPECT_TRUE(ui->controller()->busy());
    EXPECT_TRUE(!avatar->acceptsPointer());
    click(rectOf("user-avatar").center());
    EXPECT_TRUE(test::runUntil([&] { return settled(false); }, std::chrono::milliseconds(1000)));
}

TEST(AuthUiTest, PasswordPillIsOnWholeDevicePixels)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 601), 1.5}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    ui.refresh();
    const auto image = ui.renderImage();
    EXPECT_TRUE(!image.isNull());
    const base::RectF field = ui.fieldBounds();
    EXPECT_EQ(std::fmod(field.y() * 1.5, 1.0), 0.5);
    const int centre = base::roundToInt(field.center().x() * 1.5);
    int top = -1, bottom = -1;
    for (int y = base::roundToInt(field.y() * 1.5) - 3; y < base::roundToInt(field.bottom() * 1.5) + 3;
         ++y) {
        if (isPainted(image, centre, y)) {
            bottom = y;
            if (top < 0)
                top = y;
        }
    }
    EXPECT_TRUE(top > 0 && bottom > top + 4);
    EXPECT_EQ(image.pixel(centre, bottom), image.pixel(centre, top));
    EXPECT_TRUE(image.pixel(centre, top) != image.pixel(centre, top + 1));
    const auto fill = image.pixel(centre, (top + bottom) / 2);
    for (int y = top + 1; y < bottom; ++y)
        EXPECT_EQ(image.pixel(centre, y), fill);
}

TEST(AuthUiTest, OutputChangesReuseOneOffscreenWindow)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    controller.requestLock();
    ui.refresh();
    auto *window = ui.window();
    ASSERT_TRUE(window);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)},
                           {"two", base::Rect(800, 0, 640, 480)}});
    ui.refresh();
    EXPECT_EQ(ui.window(), window);
    EXPECT_EQ(secure.authContainer()->children().size(), size_t(2));
    controller.setOutputs({{"two", base::Rect(800, 0, 640, 480)}});
    ui.refresh();
    EXPECT_EQ(ui.window(), window);
    EXPECT_TRUE(base::Rect(800, 0, 640, 480)
                    .contains(secure.authContainer()->children().front()->geometry()));
    controller.setOutputs({});
    ui.refresh();
    EXPECT_TRUE(ui.renderImage().isNull());
    controller.setOutputs({{"two", base::Rect(0, 0, 640, 480)}});
    ui.refresh();
    EXPECT_TRUE(!ui.renderImage().isNull());
    EXPECT_EQ(ui.window(), window);
}

TEST(AuthUiTest, PanelTextureFollowsTheImageSize)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    controller.requestLock();
    ui.refresh();
    secure.finishTransition();
    testsupport::FakeRenderer renderer;
    const auto panelTexture = [&]() -> const render::Texture * {
        renderer.calls.clear();
        root.render(renderer);
        const base::Rect panel = secure.authContainer()->children().front()->geometry();
        for (auto call = renderer.calls.crbegin(); call != renderer.calls.crend(); ++call) {
            if (call->texture && call->rect == panel)
                return call->texture;
        }
        return nullptr;
    };
    const render::Texture *texture = panelTexture();
    ASSERT_TRUE(texture);
    EXPECT_EQ(texture->size(), base::Size(480, 600));
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600), 2}});
    ui.refresh();
    texture = panelTexture();
    ASSERT_TRUE(texture);
    EXPECT_EQ(sizeOf(ui.renderImage()), base::Size(960, 1200));
    EXPECT_EQ(texture->size(), base::Size(960, 1200));
}

TEST(AuthUiTest, TextureFailureKeepsCompositorShield)
{
    class FailedRenderer : public testsupport::FakeRenderer
    {
    public:
        std::unique_ptr<render::Texture> createTexture(const base::Size &, const uint8_t *, int,
                                                       render::PixelFormat) override
        {
            return {};
        }
    } renderer;

    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    controller.requestLock();
    ui.refresh();
    secure.finishTransition();
    root.render(renderer);
    EXPECT_EQ(renderer.calls.size(), 1);
    EXPECT_EQ(renderer.calls.front().kind, "fill");
    EXPECT_EQ(renderer.calls.front().color.a, 1.0f);
    EXPECT_EQ(renderer.calls.front().color.r, 0.0f);
    EXPECT_TRUE(controller.isBlocked());
}

TEST(AuthUiTest, SuccessDuringLockingWaitsForSecurePresentation)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    controller.requestLock();
    ui.refresh();
    ui.controller()->authenticate("valid");
    backend.succeed();
    EXPECT_EQ(controller.state(), session::SessionState::Locking);
    controller.notifyFramePresented();
    ui.refresh();
    EXPECT_EQ(controller.state(), session::SessionState::Unlocking);
    backend.succeed();
    EXPECT_EQ(backend.acceptCount, 1);
    controller.notifyFramePresented();
    EXPECT_EQ(controller.state(), session::SessionState::Active);
}

TEST(AuthUiTest, ALockThatCancelsTheUnlockAsksForThePasswordAgain)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    controller.requestLock();
    controller.notifyFramePresented();
    ui.refresh();
    ASSERT_EQ(controller.state(), session::SessionState::Locked);
    ui.controller()->authenticate("valid");
    backend.succeed();
    ASSERT_EQ(controller.state(), session::SessionState::Unlocking);

    EXPECT_TRUE(controller.requestLock());
    EXPECT_EQ(controller.state(), session::SessionState::Locking);
    backend.succeed();
    controller.notifyFramePresented();
    ui.refresh();
    EXPECT_EQ(controller.state(), session::SessionState::Locked);
    EXPECT_TRUE(fieldText(ui).empty());

    ui.controller()->authenticate("valid");
    backend.succeed();
    EXPECT_EQ(controller.state(), session::SessionState::Unlocking);
    controller.notifyFramePresented();
    EXPECT_EQ(controller.state(), session::SessionState::Active);
}

TEST(AuthUiTest, LockingCoversEveryOutput)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    controller.requestLock();
    ui.refresh();
    EXPECT_TRUE(root.isVisible());
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)},
                           {"two", base::Rect(800, 0, 640, 480)}});
    ui.refresh();
    EXPECT_EQ(secure.shieldContainer()->children().size(), size_t(2));
    controller.notifyOutputPresented("one");
    EXPECT_EQ(controller.state(), session::SessionState::Locking);
    controller.notifyOutputPresented("two");
    EXPECT_EQ(controller.state(), session::SessionState::Locked);
    controller.setOutputs({});
    ui.refresh();
    EXPECT_TRUE(controller.isBlocked());
    controller.setOutputs({{"two", base::Rect(0, 0, 640, 480)}});
    ui.refresh();
    EXPECT_TRUE(root.isVisible());
    EXPECT_EQ(secure.shieldContainer()->children().size(), size_t(1));
}

TEST(AuthUiTest, TypedPasswordIsEditedAndRetriedAfterARejection)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    scene::Node applications;
    compositor::Compositor compositor(&applications);
    input::Seat seat(&compositor);
    seat.setSessionGate(&controller);
    input::InputManager input(&seat);
    FakeBackend backend;
    auth::AuthUi ui(&controller, seat.xkbTracker(), &backend);
    EXPECT_TRUE(controller.requestLock());
    ui.refresh();
    const auto type = [&](std::initializer_list<uint32_t> keys) {
        for (uint32_t key : keys) {
            input.onKey({}, key, true);
            input.onKey({}, key, false);
        }
    };
    const uint64_t empty = ui.imageGeneration();
    type({KEY_P, KEY_W, KEY_D, KEY_X, KEY_BACKSPACE});
    EXPECT_EQ(fieldText(ui), "pwd");
    EXPECT_TRUE(!ui.renderImage().isNull());
    EXPECT_TRUE(ui.imageGeneration() > empty);
    type({KEY_ENTER});
    EXPECT_EQ(backend.authenticateCount, 1);
    EXPECT_EQ(backend.lastSecret, std::string("pwd"));
    const base::RectF field = ui.fieldBounds();
    backend.reject();
    EXPECT_TRUE(ui.controller()->failed());
    const auto fieldNow = [&] {
        ui.renderImage();
        return ui.fieldBounds();
    };
    EXPECT_TRUE(test::runUntil([&] { return !base::fuzzyCompare(fieldNow(), field); },
                               std::chrono::milliseconds(1000)));
    EXPECT_TRUE(test::runUntil([&] { return base::fuzzyCompare(fieldNow(), field); },
                               std::chrono::milliseconds(2000)));
    type({KEY_O, KEY_K, KEY_ENTER});
    EXPECT_EQ(backend.authenticateCount, 2);
    EXPECT_EQ(backend.lastSecret, std::string("ok"));
    backend.succeed();
    controller.notifyFramePresented();
    EXPECT_TRUE(test::runUntil(
            [&] { return controller.state() == session::SessionState::Unlocking; }));
    seat.shutdown();
}

TEST(AuthUiTest, DoubleClickSelectsTheWholePasswordToRetype)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    scene::Node applications;
    compositor::Compositor compositor(&applications);
    input::Seat seat(&compositor);
    seat.setSessionGate(&controller);
    input::InputManager input(&seat);
    FakeBackend backend;
    auth::AuthUi ui(&controller, seat.xkbTracker(), &backend);
    EXPECT_TRUE(controller.requestLock());
    ui.refresh();
    const auto type = [&](std::initializer_list<uint32_t> keys) {
        for (uint32_t key : keys) {
            input.onKey({}, key, true);
            input.onKey({}, key, false);
        }
    };
    type({KEY_P, KEY_W, KEY_D});
    ASSERT_EQ(fieldText(ui), "pwd");
    const base::PointF panel(secure.authContainer()->children().front()->absolutePosition());
    const base::PointF onField = panel + ui.fieldBounds().center();
    ui.onPointerMotion({}, onField);
    for (int i = 0; i < 2; ++i) {
        ui.onPointerButton({}, onField, BTN_LEFT, true);
        ui.onPointerButton({}, onField, BTN_LEFT, false);
    }
    const ui::TextField *field = focusedField(ui);
    ASSERT_TRUE(field);
    EXPECT_TRUE(field->hasSelection());
    type({KEY_O, KEY_K});
    EXPECT_EQ(fieldText(ui), "ok");
    seat.shutdown();
}

TEST(AuthUiTest, SubmitButtonKeepsTheArrow)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    scene::Node applications;
    compositor::Compositor compositor(&applications);
    input::Seat seat(&compositor);
    seat.setSessionGate(&controller);
    input::InputManager input(&seat);
    FakeBackend backend;
    auth::AuthUi ui(&controller, seat.xkbTracker(), &backend);
    EXPECT_TRUE(controller.requestLock());
    ui.refresh();
    std::vector<input::CursorShape> cursors;
    auto cursorSubscription = ui.cursorShapeChanged.connect(
            [&](input::CursorShape shape) { cursors.push_back(shape); });
    const base::PointF panel(secure.authContainer()->children().front()->absolutePosition());
    const base::RectF field = ui.fieldBounds();
    const base::PointF go = panel + base::PointF(field.right() - 4 - 12, field.center().y());
    ui.onPointerMotion({}, go);
    EXPECT_TRUE(cursors.empty() || cursors.back() == input::CursorShape::Default);
    input.onKey({}, KEY_A, true);
    input.onKey({}, KEY_A, false);
    ui.onPointerMotion({}, panel + field.center());
    EXPECT_EQ(cursors.back(), input::CursorShape::Text);
    ui.onPointerMotion({}, go + base::PointF(1, 0));
    EXPECT_EQ(cursors.back(), input::CursorShape::Default);
    ui.onPointerButton({}, go, BTN_LEFT, true);
    ui.onPointerButton({}, go, BTN_LEFT, false);
    EXPECT_EQ(backend.authenticateCount, 1);
    EXPECT_EQ(backend.lastSecret, std::string("a"));
    seat.shutdown();
}

TEST(AuthUiTest, PanelOpensOnThePointersScreen)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    const base::Rect one(0, 0, 800, 600);
    const base::Rect two(800, 0, 1000, 700);
    controller.setOutputs({{"one", one}, {"two", two, 1, true}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    base::PointF pointer(-10, -10);
    ui.setPointerLocator([&] {
        return pointer;
    });
    EXPECT_TRUE(controller.requestLock());
    EXPECT_TRUE(test::runUntil([&] { return ui.isPanelDrawn(); }));
    const scene::Node *panel = secure.authContainer()->children().front();
    EXPECT_TRUE(two.contains(panel->absoluteGeometry()));
    pointer = base::PointF(100, 100);
    ui.onPointerMotion({}, pointer);
    EXPECT_TRUE(ui.isRefreshPending());
    EXPECT_TRUE(test::runUntil([&] { return one.contains(panel->absoluteGeometry()); }));
    const base::Rect placed = panel->absoluteGeometry();
    pointer = base::PointF(300, 300);
    ui.onPointerMotion({}, pointer);
    test::runFor(std::chrono::milliseconds(50));
    EXPECT_EQ(panel->absoluteGeometry(), placed);
}

TEST(AuthUiTest, LoginUsesSameOffscreenScreenAndStaysBlockedForHandoff)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure, nullptr, session::SessionState::Login);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    ui.refresh();
    EXPECT_TRUE(!ui.renderImage().isNull());
    EXPECT_EQ(ui.controller()->mode(), auth::AuthController::Login);
    ui.controller()->authenticate("valid");
    backend.succeed();
    EXPECT_EQ(backend.acceptCount, 1);
    EXPECT_EQ(controller.state(), session::SessionState::Login);
    EXPECT_TRUE(root.isVisible());
}

TEST(AuthUiTest, StatusIconsSitInTheTopRightCornerAndTakeNoInput)
{
    withoutNetworkManager();
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    const base::Rect one(0, 0, 800, 600);
    const base::Rect two(800, 0, 1280, 800);
    controller.setOutputs({{"one", one, 1, true}, {"two", two}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    base::PointF pointer(100, 100);
    ui.setPointerLocator([&] {
        return pointer;
    });
    EXPECT_TRUE(controller.requestLock());
    ui.refresh();
    auth::AuthStatus *status = ui.status();
    ASSERT_TRUE(status);
    shell::MenuBarIndicators::Network network;
    network.wirelessHwEnabled = true;
    network.wirelessEnabled = true;
    network.wirelessName = "Home";
    network.wirelessIcon = "network-wireless-connected-75";
    status->networkIcon()->showNetwork(network);
    status->batteryIcon()->setVisible(true);
    status->batteryIcon()->setIconName("battery-level-60-symbolic");
    status->batteryIcon()->setLabel("60%");
    ui.refresh();

    ASSERT_EQ(secure.authContainer()->children().size(), size_t(2));
    const scene::Node *node = secure.authContainer()->children().back();
    EXPECT_EQ(node->geometry().x() + node->geometry().width(), one.width());
    EXPECT_EQ(node->geometry().y(), 0);
    EXPECT_EQ(node->geometry().height(), 32);
    EXPECT_EQ(sizeOf(ui.renderStatusImage()), node->geometry().size());

    ui.statusWindow()->updateLayout();
    const shell::StatusIcon *battery = status->batteryIcon();
    const shell::StatusIcon *wifi = status->networkIcon();
    EXPECT_DOUBLE_EQ(battery->geometry().x + battery->geometry().width, double(node->geometry().width()));
    EXPECT_DOUBLE_EQ(battery->rightInset(), 9);
    EXPECT_DOUBLE_EQ(wifi->rightInset(), 3);
    EXPECT_DOUBLE_EQ(wifi->geometry().x + wifi->geometry().width, battery->geometry().x);
    EXPECT_EQ(wifi->glyph(), "network-wireless-connected-75");

    secure.finishTransition();
    testsupport::FakeRenderer renderer;
    root.render(renderer);
    const auto drawn = std::find_if(renderer.calls.begin(), renderer.calls.end(),
                                    [&](const auto &call) { return call.texture && call.rect == node->geometry(); });
    ASSERT_TRUE(drawn != renderer.calls.end());
    EXPECT_EQ(drawn->kind, "texture");
    EXPECT_EQ(drawn->alpha, 1.0f);
    EXPECT_EQ(drawn->texture->size(), node->geometry().size());

    const base::PointF onBattery(node->geometry().x() + battery->geometry().x + battery->geometry().width / 2, 16);
    ui.onPointerMotion({}, onBattery);
    ui.onPointerButton({}, onBattery, BTN_LEFT, true);
    EXPECT_TRUE(!battery->isPressed());
    EXPECT_TRUE(!battery->isPlateShown());
    ui.onPointerButton({}, onBattery, BTN_LEFT, false);
    EXPECT_EQ(backend.authenticateCount, 0);

    pointer = base::PointF(1000, 300);
    ui.onPointerMotion({}, pointer);
    EXPECT_TRUE(test::runUntil([&] { return node->geometry().x() + node->geometry().width() == two.right(); }));
    EXPECT_EQ(node->geometry().y(), 0);

    network.wirelessHwEnabled = false;
    status->networkIcon()->showNetwork(network);
    status->batteryIcon()->setVisible(false);
    ui.refresh();
    EXPECT_TRUE(!node->isVisible());
}

TEST(AuthUiTest, StatusIconsAreLightOnADarkWallpaperAndDarkOnALightOne)
{
    test::TemporaryDirectory files;
    ASSERT_TRUE(files.isValid());
    const auto background = [&](const std::string &name, const std::string &color) {
        base::Settings settings(files.filePath(name));
        settings.setValue("Appearance/BackgroundType", 1);
        settings.setValue("Appearance/BackgroundColor", color);
        settings.sync();
        return settings.path();
    };
    const std::string dark = background("dark.conf", "#202020");
    const std::string light = background("light.conf", "#F0F0F0");
    scene::Node root;
    session::SecureContainer secure(&root);
    secure.setSettingsFile(dark, false);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    EXPECT_TRUE(controller.requestLock());
    ui.refresh();
    ASSERT_TRUE(ui.status());
    EXPECT_TRUE(test::runUntil([&] { return ui.status()->lightContents(); }));
    secure.setSettingsFile(light, true);
    EXPECT_TRUE(test::runUntil([&] { return !ui.status()->lightContents(); }));
}

TEST(AuthUiTest, AnEnrolledFingerLeadsAndAnyKeyBringsOutThePassword)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    scene::Node applications;
    compositor::Compositor compositor(&applications);
    input::Seat seat(&compositor);
    seat.setSessionGate(&controller);
    input::InputManager input(&seat);
    FakeBackend backend;
    backend.hasReader = true;
    backend.enrolled = true;
    auth::AuthUi ui(&controller, seat.xkbTracker(), &backend);
    // Animations advance on rendered frames.
    const auto revealed = [&] {
        return test::runUntil(
                [&] {
                    ui.renderImage();
                    return ui.fieldOpacity() == 1;
                },
                std::chrono::milliseconds(500));
    };
    const auto press = [&](int key) {
        input.onKey({}, key, true);
        input.onKey({}, key, false);
    };

    ASSERT_TRUE(controller.requestLock());
    controller.notifyFramePresented();
    ui.refresh();
    ui::View *screen = ui.window()->contentView();
    ui::View *field = findView(screen, "password-field");
    ui::View *caption = findView(screen, "fingerprint-caption");
    ui::View *hint = findView(screen, "fingerprint-hint");
    ASSERT_TRUE(field && caption && hint);
    EXPECT_TRUE(backend.readerRunning);
    // The text holds the field's place, and the row under it is kept empty.
    EXPECT_EQ(ui.fieldOpacity(), 0);
    EXPECT_EQ(caption->opacity(), 1);
    EXPECT_TRUE(hint->isVisible());
    EXPECT_EQ(hint->opacity(), 0);
    ui.window()->updateLayout();
    const base::RectF fieldRect = panelRect(field);
    const base::RectF hintRect = panelRect(hint);
    EXPECT_NEAR(panelRect(caption).center().y(), fieldRect.center().y(), 0.5);
    EXPECT_GT(hintRect.y(), fieldRect.bottom());

    press(KEY_A);
    EXPECT_EQ(fieldText(ui), "a");
    EXPECT_TRUE(revealed());
    EXPECT_EQ(caption->opacity(), 0);
    EXPECT_EQ(hint->opacity(), 1);
    ui.window()->updateLayout();
    EXPECT_EQ(panelRect(field), fieldRect);
    EXPECT_EQ(panelRect(hint), hintRect);
    seat.shutdown();
}

TEST(AuthUiTest, ThreeMisreadFingersBringOutThePassword)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    scene::Node applications;
    compositor::Compositor compositor(&applications);
    input::Seat seat(&compositor);
    seat.setSessionGate(&controller);
    input::InputManager input(&seat);
    FakeBackend backend;
    backend.hasReader = true;
    backend.enrolled = true;
    auth::AuthUi ui(&controller, seat.xkbTracker(), &backend);
    // Animations advance on rendered frames.
    const auto revealed = [&] {
        return test::runUntil(
                [&] {
                    ui.renderImage();
                    return ui.fieldOpacity() == 1;
                },
                std::chrono::milliseconds(500));
    };

    ASSERT_TRUE(controller.requestLock());
    controller.notifyFramePresented();
    ui.refresh();
    backend.mismatch();
    backend.mismatch();
    EXPECT_FALSE(revealed());
    backend.mismatch();
    EXPECT_TRUE(revealed());
    EXPECT_EQ(controller.state(), session::SessionState::Locked);
    seat.shutdown();
}

TEST(AuthUiTest, AFingerFoundBeforeTheLockOpensOnTheReaderWithoutAChange)
{
    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    backend.hasReader = true;
    auth::AuthUi ui(&controller, nullptr, &backend);
    ui.refresh();
    ASSERT_TRUE(ui.window());
    EXPECT_EQ(backend.enrollmentCheckCount, 1);
    EXPECT_EQ(ui.fieldOpacity(), 1);

    backend.setEnrolled(true);
    EXPECT_EQ(ui.fieldOpacity(), 0);
    EXPECT_FALSE(backend.readerRunning);

    EXPECT_TRUE(controller.requestLock());
    EXPECT_TRUE(test::runUntil([&] { return backend.readerRunning; }));
    EXPECT_EQ(ui.fieldOpacity(), 0);
}

TEST(AuthUiTest, LoginLanguageFollowsThePickedUser)
{
    const auto saved = auth::currentLanguageEnvironment();
    test::TemporaryDirectory backgrounds;
    ASSERT_TRUE(backgrounds.isValid());
    auth::setLoginBackgroundDirectoryForTesting(backgrounds.path());
    struct Guard {
        std::map<std::string, std::string> env;
        ~Guard()
        {
            auth::setLoginBackgroundDirectoryForTesting({});
            toolkit::application().setLanguageEnvironment(env);
        }
    } guard {saved};

    base::Settings alice(backgrounds.filePath("alice.conf"));
    alice.setValue("Language/LANG", "zh_TW.UTF-8");
    alice.setValue("Language/LANGUAGE", "zh_TW:en_US");
    ASSERT_TRUE(alice.sync());

    const auto loaded = auth::loginLanguageEnvironment("alice");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->at("LANG"), "zh_TW.UTF-8");
    EXPECT_EQ(loaded->at("LANGUAGE"), "zh_TW:en_US");
    EXPECT_FALSE(auth::loginLanguageEnvironment("bob").has_value());

    scene::Node root;
    session::SecureContainer secure(&root);
    session::SessionController controller(&secure, nullptr, session::SessionState::Login);
    controller.setOutputs({{"one", base::Rect(0, 0, 800, 600)}});
    FakeBackend backend;
    auth::AuthUi ui(&controller, nullptr, &backend);
    ui.refresh();
    ASSERT_TRUE(ui.controller());
    ui.controller()->setUsers({"alice", "bob"});
    ui.controller()->setUserIndex(0);
    EXPECT_EQ(std::string(std::getenv("LANGUAGE") ? std::getenv("LANGUAGE") : ""), "zh_TW:en_US");

    ui.controller()->setUserIndex(1);
    EXPECT_EQ(std::string(std::getenv("LANGUAGE") ? std::getenv("LANGUAGE") : ""),
              saved.at("LANGUAGE"));
}
