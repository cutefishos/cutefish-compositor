// SPDX-License-Identifier: BSD-3-Clause
#include "shell/spotlight/spotlight.h"

#include <algorithm>
#include <cmath>

#include "core/main_loop.h"
#include "input/seat.h"
#include "shell/clipboard/clipboard_history.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "shell/spotlight/spotlight_view.h"
#include "ui/embedded_window.h"
#include "ui/theme.h"
#include "wm/window_manager.h"

namespace cutefish::shell {

Spotlight::Spotlight(Shell *shell, Dock *dock, std::unique_ptr<ClipboardHistory> clipboard, std::string settingsPath)
    : m_shell(shell)
    , m_clipboard(std::move(clipboard))
    , m_window(std::make_unique<ShellWindow>(shell, wm::WindowRole::Picker))
{
    auto view = std::make_unique<SpotlightView>(*shell, dock, *m_clipboard, std::move(settingsPath));
    m_view = view.get();
    m_view->onDismiss = [this] { hide(); };
    m_view->onResize = [this] { fit(); };
    m_view->onPaint = [this] { applyGlass(); };
    m_window->setContentView(std::move(view));
    m_window->window()->setBackgroundColor(ui::Color{0, 0, 0, 0});
    m_window->onCloseRequested = [this] { hide(); };
    m_window->onKeyboardFocusChanged = [this](bool focused) {
        if (focused) {
            m_hadKeyboard = true;
        } else if (m_hadKeyboard && m_window->isShown()) {
            // A popup takes the keyboard from its parent, too. Let it map first.
            std::weak_ptr<bool> alive = m_alive;
            core::mainLoop()->post([this, alive] {
                if (!alive.expired() && m_hadKeyboard && m_window->isShown() && !m_view->menuOpen() && !m_view->dragging()
                    && m_shell->seat() && m_shell->seat()->focusedSurface() != m_window->surface())
                    hide();
            });
        }
    };
    m_clipboard->togglePicker = [this] { toggle(Mode::Clipboard); };
    m_clipboard->closePicker = [this] { hide(); };
    m_clipboard->sendPaste = [this] { m_shell->windowManager()->sendPaste(); };
    m_motion.onTimeout = [this] { place(); };
    m_retention.onTimeout = [this] {
        constexpr auto retention = std::chrono::hours(8);
        const auto cutoff = std::chrono::system_clock::now() - retention;
        m_clipboard->store().expireBefore(std::chrono::duration_cast<std::chrono::milliseconds>(cutoff.time_since_epoch()).count());
    };
    m_retention.start(std::chrono::minutes(1));
    m_subscriptions.push_back(m_clipboard->store().changed.connect([this, loaded = false]() mutable {
        if (!loaded && m_clipboard->store().isLoaded()) {
            loaded = true;
            m_retention.onTimeout();
        }
    }));
    m_subscriptions.push_back(shell->outputsChanged.connect([this] {
        if (m_window->isShown())
            fit();
    }));
}

Spotlight::~Spotlight()
{
    m_alive.reset();
    m_subscriptions.clear();
    if (m_service)
        m_shell->bus()->releaseName("org.cutefish.Spotlight");
    if (m_legacy)
        m_shell->bus()->releaseName("org.cutefish.Launchpad");
    m_window->onKeyboardFocusChanged = nullptr;
    m_window.reset();
}

void Spotlight::registerServices()
{
    auto *bus = m_shell->bus();
    if (!bus)
        return;
    const auto publish = [bus](const char *name, const char *path, std::unique_ptr<base::dbus::Object> &slot,
                               Spotlight *owner, Mode mode) {
        if (slot)
            return;
        auto object = std::make_unique<base::dbus::Object>(bus, path, name);
        object->addMethod("show", [owner, mode] { owner->show(mode); });
        object->addMethod("hide", [owner] { owner->hide(); });
        object->addMethod("toggle", [owner, mode] { owner->toggle(mode); });
        if (object->publish() && bus->requestName(name, base::dbus::Connection::DoNotQueue))
            slot = std::move(object);
    };
    publish("org.cutefish.Spotlight", "/Spotlight", m_service, this, Mode::Search);
    publish("org.cutefish.Launchpad", "/Launchpad", m_legacy, this, Mode::Applications);
    m_clipboard->registerService();
}

void Spotlight::show(Mode mode)
{
    const bool opening = !m_window->isShown();
    if (opening) {
        const Shell::Output *output = m_shell->seat()
                ? m_shell->outputAt(m_shell->seat()->cursorPosition().toPoint()) : nullptr;
        m_output = output ? output->name : std::string();
        m_hadKeyboard = false;
    }
    m_view->setMode(mode);
    fit();
    if (opening && !m_target.isEmpty()) {
        m_initialHeight = std::min(m_target.height(), SpotlightView::kHeader + 46);
        m_initialYOffset = -8;
        m_motionStart = std::chrono::steady_clock::now();
        m_motion.start(std::chrono::milliseconds(16));
        place();
    }
    m_window->show();
    m_window->activate();
    m_view->focusSearch();
}

void Spotlight::hide()
{
    if (!m_window->isShown())
        return;
    m_hadKeyboard = false;
    m_motion.stop();
    m_view->closeMenu();
    m_window->hide();
    m_view->reset();
}

void Spotlight::toggle(Mode mode)
{
    if (m_window->isShown() && (mode == Mode::Search || mode == m_view->mode()))
        hide();
    else
        show(mode);
}

void Spotlight::fit()
{
    const Shell::Output *output = m_shell->output(m_output);
    if (!output)
        output = m_shell->output({});
    if (!output)
        return;
    const base::Rect area = m_shell->workArea(*output);
    const int width = std::clamp(area.width() - 32, 200, SpotlightView::kWidth);
    const int top = std::clamp(area.height() * 22 / 100, 12, 240);
    const int height = std::min(m_view->preferredHeight(), std::max(SpotlightView::kHeader, area.height() - top - 18));
    m_window->setOutput(output->name);
    const base::Rect target(area.x() + (area.width() - width) / 2, area.y() + top, width, height);
    if (target != m_target && m_window->isShown() && target.topLeft() == m_target.topLeft()
        && target.width() == m_target.width() && !m_view->idle()) {
        m_initialHeight = m_window->size().height();
        m_initialYOffset = m_window->geometry().y() - target.y();
        m_motionStart = std::chrono::steady_clock::now();
        m_motion.start(std::chrono::milliseconds(16));
    } else if (target != m_target) {
        m_motion.stop();
    }
    m_target = target;
    place();
}

void Spotlight::place()
{
    if (m_target.isEmpty())
        return;
    base::Rect geometry = m_target;
    if (m_motion.isActive()) {
        const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_motionStart).count();
        const double remaining = std::pow(std::max(0.0, 1 - elapsed / 160.0), 3);
        geometry.setHeight(int(std::lround(geometry.height() + (m_initialHeight - geometry.height()) * remaining)));
        geometry.translate(0, int(std::lround(m_initialYOffset * remaining)));
        if (remaining == 0)
            m_motion.stop();
    }
    m_window->setGeometry(geometry);
    applyGlass();
}

void Spotlight::applyGlass()
{
    const bool dark = ui::Theme::current().darkMode;
    const bool idle = m_view->idle();
    const base::Size size = m_window->size();
    if (size.isEmpty() || (size == m_glassSize && idle == m_glassIdle && dark == m_glassDark))
        return;
    m_glassSize = size;
    m_glassIdle = idle;
    m_glassDark = dark;
    const ui::Path shape = m_view->glassShape(size.width(), size.height());
    const base::Rect bounds(0, 0, size.width(), size.height());
    m_window->setInputRegion(m_view->inputRegion(size.width(), size.height()));
    // FishUI PopupGlass. Neutral tint; the hue of the backdrop is not baked into the panel.
    auto glass = dark ? popupGlass(0.10, 0.10, 0.10, 0.70) : popupGlass(1, 1, 1, 0.66);
    glass.blurSigma = glassFixed(8);
    m_window->setBackgroundBlur(bounds, idle ? SpotlightView::kHeader / 2 : 24, glass, shape);
}

} // namespace cutefish::shell
