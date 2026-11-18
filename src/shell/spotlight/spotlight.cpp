// SPDX-License-Identifier: BSD-3-Clause
#include "shell/spotlight/spotlight.h"

#include <algorithm>
#include <cmath>

#include "compositor/surface.h"
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
    m_motion = std::make_unique<ui::Animation>(m_window->window());
    m_motion->onFinished = [this] {
        if (m_closing)
            finishHide();
    };
    m_window->onCloseRequested = [this] { hide(); };
    m_window->onKeyboardFocusChanged = [this](bool focused) {
        if (focused) {
            m_hadKeyboard = true;
        } else if (m_hadKeyboard && !m_closing && m_window->isShown()) {
            // A popup takes the keyboard from its parent, too. Let it map first.
            std::weak_ptr<bool> alive = m_alive;
            core::mainLoop()->post([this, alive] {
                if (!alive.expired() && m_hadKeyboard && !m_closing && m_window->isShown() && !m_view->menuOpen() && !m_view->dragging()
                    && m_shell->seat() && m_shell->seat()->focusedSurface() != m_window->surface())
                    hide();
            });
        }
    };
    m_clipboard->togglePicker = [this] { toggle(Mode::Clipboard); };
    m_clipboard->closePicker = [this] { hide(); };
    m_clipboard->sendPaste = [this] { m_shell->windowManager()->sendPaste(); };
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
        if (m_window->isShown() && !m_closing)
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
    m_motion.reset();
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
    if (!m_shell->windowManager()->allowsActivation())
        return;
    const bool opening = !m_window->isShown();
    const bool reopening = m_closing;
    m_closing = false;
    if (opening) {
        const Shell::Output *output = m_shell->seat()
                ? m_shell->outputAt(m_shell->seat()->cursorPosition().toPoint()) : nullptr;
        m_output = output ? output->name : std::string();
        m_hadKeyboard = false;
    }
    m_preparing = true;
    if (!reopening || mode != m_view->mode())
        m_view->setMode(mode);
    m_preparing = false;
    fit();
    if (m_target.isEmpty())
        return;
    if (opening) {
        auto entry = m_target.translated(0, -7);
        entry.setHeight(std::min(entry.height(), SpotlightView::kHeader + 36));
        place(entry, 0, m_expanded);
        m_window->setInputRegion(m_view->inputRegion(entry.width(), entry.height()));
        m_window->show();
        animateTo(m_target, 1, m_expanded);
    } else if (reopening) {
        const auto size = m_window->size();
        m_window->setInputRegion(m_view->inputRegion(size.width(), size.height()));
        animateTo(m_target, 1, m_view->idle() ? 0 : 1);
    }
    m_window->activate();
    m_view->focusSearch();
}

void Spotlight::hide()
{
    if (!m_window->isShown())
        return;
    if (!m_shell->windowManager()->allowsActivation()) {
        finishHide();
        return;
    }
    if (m_closing)
        return;
    m_closing = true;
    m_hadKeyboard = false;
    m_view->closeMenu();
    m_view->stopScrolling();
    m_window->setInputRegion(base::Region());
    m_shell->windowManager()->clearKeyboardGrab(m_window->wmWindow());
    // Do not resize the focused content on dismissal. Leave the same glass
    // silhouette in place as it lifts and fades, without intercepting input.
    animateTo(m_window->geometry().translated(0, -6), 0, m_expanded, true);
}

void Spotlight::finishHide()
{
    m_hadKeyboard = false;
    m_motion->stop();
    m_window->hide();
    m_closing = false;
    m_view->reset();
}

void Spotlight::toggle(Mode mode)
{
    if (m_window->isShown() && !m_closing && (mode == Mode::Search || mode == m_view->mode()))
        hide();
    else
        show(mode);
}

void Spotlight::fit()
{
    if (m_preparing || m_closing)
        return;
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
    const bool moved = m_target.topLeft() != target.topLeft() || m_target.width() != target.width();
    const double expanded = m_view->idle() ? 0 : 1;
    if (!m_window->isShown() || moved) {
        m_motion->stop();
        place(target, 1, expanded);
    } else if (target != m_target || expanded != m_targetExpanded) {
        animateTo(target, 1, expanded);
    }
    m_target = target;
    m_targetExpanded = expanded;
}

void Spotlight::animateTo(base::Rect bounds, double opacity, double expanded, bool closing)
{
    using namespace std::chrono_literals;
    const auto from = m_window->geometry();
    m_motion->setDuration(closing ? 120ms : 205ms);
    m_motion->setEasing(closing ? ui::Animation::Easing::InOutQuad : ui::Animation::Easing::OutCubic);
    m_motion->onUpdate = [this, from, bounds, fromOpacity = m_opacity, opacity,
                          fromExpanded = m_expanded, expanded](double t) {
        auto frame = bounds;
        frame.moveTop(int(std::lround(std::lerp(from.y(), bounds.y(), t))));
        frame.setHeight(int(std::lround(std::lerp(from.height(), bounds.height(), t))));
        place(frame, std::lerp(fromOpacity, opacity, t), std::lerp(fromExpanded, expanded, t));
    };
    m_motion->start();
}

void Spotlight::place(base::Rect bounds, double opacity, double expanded)
{
    m_opacity = opacity;
    m_expanded = expanded;
    m_window->setGeometry(bounds);
    m_window->surface()->setOpacity(float(opacity));
    m_view->setExpansion(expanded);
    applyGlass();
}

void Spotlight::applyGlass()
{
    const bool dark = ui::Theme::current().darkMode;
    const base::Size size = m_window->size();
    const bool shapeChanged = size != m_glassSize || m_expanded != m_glassExpanded;
    if (size.isEmpty() || (!shapeChanged && m_opacity == m_glassOpacity && dark == m_glassDark))
        return;
    m_glassSize = size;
    m_glassExpanded = m_expanded;
    m_glassOpacity = m_opacity;
    m_glassDark = dark;
    const ui::Path shape = m_view->glassShape(size.width(), size.height());
    const base::Rect bounds(0, 0, size.width(), size.height());
    if (!m_closing && shapeChanged)
        m_window->setInputRegion(m_view->inputRegion(size.width(), size.height()));
    // FishUI PopupGlass. Neutral tint; the hue of the backdrop is not baked into the panel.
    auto glass = dark ? popupGlass(0.10, 0.10, 0.10, 0.70) : popupGlass(1, 1, 1, 0.66);
    glass.blurSigma = glassFixed(8);
    glass.opacity = float(m_opacity);
    m_window->setBackgroundBlur(bounds, 24, glass, shape);
}

} // namespace cutefish::shell
