// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/controlcenter/power_dialog.h"

#include <cmath>

#include "compositor/surface.h"
#include "base/event_loop.h"
#include "base/i18n.h"
#include "core/main_loop.h"
#include "shell/shell.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "ui/dialog_window.h"
#include "ui/embedded_window.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/theme.h"
#include "wm/shell_surface.h"

using cutefish::i18n::tr;
using cutefish::i18n::trNoop;

namespace cutefish::shell {

static constexpr double kDragDistance = 10;

static const char *const kText = trNoop("Every application is asked to quit first, so you can save your work.");

class PowerDialog::Content final : public ui::View
{
public:
    explicit Content(PowerDialog *dialog)
        : m_dialog(dialog)
    {
        m_alert = addChild<ui::AlertView>();
        m_alert->setText(tr(kText));
        m_alert->cancel()->setText(tr("Cancel"));
        m_alert->cancel()->onClicked = [this] { m_dialog->finish(false); };
        m_alert->confirm()->onClicked = [this] { m_dialog->finish(true); };
    }

    ui::DialogButton *cancel() const { return m_alert->cancel(); }

    void retranslate()
    {
        m_alert->setText(tr(kText));
        m_alert->cancel()->setText(tr("Cancel"));
    }

    void setAction(Action action)
    {
        m_alert->title()->setText(PowerDialog::title(action));
        m_alert->confirm()->setText(PowerDialog::confirmText(action));
        m_alert->requestLayout();
    }

    double width() const { return ui::AlertView::kWidth; }

    double height() const { return m_alert->heightForWidth(width()); }

    bool acceptsPointer() const override { return true; }

protected:
    void layout() override { m_alert->setGeometry(localRect()); }

    void paint(ui::Painter &) override
    {
        const uint64_t generation = ui::Theme::generation();
        if (generation == m_theme)
            return;
        m_theme = generation;
        m_dialog->updateGlass();
    }

    void pointerPress(const ui::PointerEvent &event) override
    {
        if (event.button != ui::MouseButton::Left)
            return;
        m_pressed = true;
        m_pressPosition = event.position;
    }

    void pointerMove(const ui::PointerEvent &event) override
    {
        if (!m_pressed)
            return;
        const double dx = event.position.x - m_pressPosition.x;
        const double dy = event.position.y - m_pressPosition.y;
        if (std::hypot(dx, dy) < kDragDistance)
            return;
        m_pressed = false;
        if (ShellWindow *window = m_dialog->window(); window && window->shellSurface())
            window->shellSurface()->startMove();
    }

    void pointerRelease(const ui::PointerEvent &) override { m_pressed = false; }

private:
    PowerDialog *m_dialog;
    ui::AlertView *m_alert;
    bool m_pressed = false;
    ui::PointF m_pressPosition;
    uint64_t m_theme = 0;
};

PowerDialog::PowerDialog(Shell *shell)
    : m_shell(shell)
{
}

PowerDialog::~PowerDialog() = default;

std::string PowerDialog::title(Action action)
{
    switch (action) {
    case Action::Reboot:
        return tr("Restart now?");
    case Action::Shutdown:
        return tr("Shut down now?");
    case Action::Logout:
        break;
    }
    return tr("Log out now?");
}

std::string PowerDialog::confirmText(Action action)
{
    switch (action) {
    case Action::Reboot:
        return tr("Restart");
    case Action::Shutdown:
        return tr("Shut Down");
    case Action::Logout:
        break;
    }
    return tr("Log Out");
}

bool PowerDialog::isOpen() const
{
    return m_window && m_window->isShown();
}

void PowerDialog::open(Action action)
{
    m_action = action;
    if (!m_window) {
        m_window = std::make_unique<ShellWindow>(m_shell, wm::WindowRole::Normal);
        m_window->setDialog(true);
        auto content = std::make_unique<Content>(this);
        m_content = content.get();
        m_window->setContentView(std::move(content));
        m_window->surface()->setAlwaysOnTop(true);
        m_window->onCloseRequested = [this] { finish(false); };
        ui::EmbeddedWindow *window = m_window->window();
        window->addShortcut(ui::NoModifier, ui::key::Escape, [this] { finish(false); });
        window->addShortcut(ui::NoModifier, ui::key::Return, [this] { finish(true); });
        window->addShortcut(ui::NoModifier, ui::key::KeypadEnter, [this] { finish(true); });
    }
    m_content->setAction(action);
    m_window->shellSurface()->setTitle(title(action));
    resize();
    if (m_window->isShown())
        return;
    m_window->show();
    m_window->activate();
    m_content->cancel()->setFocus();
}

void PowerDialog::retranslate()
{
    if (!m_window)
        return;
    m_content->retranslate();
    open(m_action);
}

void PowerDialog::resize()
{
    m_window->setSize(base::Size(int(m_content->width()), int(m_content->height())));
    updateGlass();
}

void PowerDialog::updateGlass()
{
    const base::Size size = m_window->size();
    const bool dark = ui::Theme::current().darkMode;
    // The panels' glass, its tint thicker: the dialog sits over the middle
    // of the screen, where the backdrop greys a thin one.
    m_window->setBackgroundBlur(base::Rect(0, 0, size.width(), size.height()),
                                int(std::lround(ui::Theme::current().windowRadius)),
                                dark ? popupGlass(0.17, 0.17, 0.19, 0.8) : popupGlass(1, 1, 1, 0.8));
}

void PowerDialog::close()
{
    if (!m_window)
        return;
    m_window->hide();
    // Not from inside its own event handling.
    std::shared_ptr<ShellWindow> doomed(std::move(m_window));
    m_content = nullptr;
    core::mainLoop()->post([doomed] {});
}

void PowerDialog::finish(bool accepted)
{
    if (!isOpen())
        return;
    close();
    if (accepted && onAccepted)
        onAccepted();
    else if (!accepted && onRejected)
        onRejected();
}

} // namespace cutefish::shell
