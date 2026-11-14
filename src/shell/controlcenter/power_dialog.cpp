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
#include "shell/shell_window.h"
#include "ui/dialog_window.h"
#include "ui/embedded_window.h"
#include "ui/keys.h"
#include "ui/color_provider.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "wm/shell_surface.h"

using cutefish::i18n::tr;
using cutefish::i18n::trNoop;

namespace cutefish::shell {

// A press that moves this far moves the window.
static constexpr double kDragDistance = 10;

static const char *const kText = trNoop("Every application is asked to quit first, so you can save your work.");

// The dialog's window content: the alert; a drag anywhere off its buttons
// moves the window.
class PowerDialog::Content final : public ui::AlertView
{
public:
    explicit Content(PowerDialog *dialog)
        : m_dialog(dialog)
    {
        setText(tr(kText));
        cancel()->setText(tr("Cancel"));
        cancel()->onClicked = [this] { m_dialog->finish(false); };
        confirm()->onClicked = [this] { m_dialog->finish(true); };
    }

    void retranslate()
    {
        setText(tr(kText));
        cancel()->setText(tr("Cancel"));
    }

    void setAction(Action action)
    {
        title()->setText(PowerDialog::title(action));
        confirm()->setText(PowerDialog::confirmText(action));
        requestLayout();
    }

    bool acceptsPointer() const override { return true; }

protected:
    void paint(ui::Painter &painter) override
    {
        painter.fillRect(localRect(), ui::color(ui::ColorId::AlertBackground));
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
    bool m_pressed = false;
    ui::PointF m_pressPosition;
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
        // Kept above applications, as the compositor's always-on-top hint.
        m_window->surface()->setAlwaysOnTop(true);
        m_window->onCloseRequested = [this] { finish(false); };
        ui::EmbeddedWindow *window = m_window->window();
        window->addShortcut(ui::NoModifier, ui::key::Escape, [this] { finish(false); });
        // Return confirms even while Cancel holds the focus.
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
    const double width = ui::AlertView::kWidth;
    m_window->setSize(base::Size(int(width), int(m_content->heightForWidth(width))));
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
