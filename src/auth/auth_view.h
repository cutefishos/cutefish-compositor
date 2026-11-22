// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_AUTH_AUTH_VIEW_H
#define CUTEFISH_AUTH_AUTH_VIEW_H

#include <ctime>
#include <memory>
#include <optional>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "base/timer.h"
#include "ui/view.h"

namespace cutefish::ui {
class Animation;
class ColumnLayout;
class Window;
} // namespace cutefish::ui

namespace cutefish::auth {

class AuthController;
class ChoiceMenu;
class ErrorLabel;
class HintLabel;
class PasswordField;
class RaisedLabel;
class UserGrid;
class UnlockPrompt;
class UserNameField;
class UserView;

class AuthScreen final : public ui::View
{
public:
    AuthScreen(ui::Window *window, AuthController *controller);
    ~AuthScreen() override;

    // The clock only ticks while blocked, so an unseen panel never wakes the compositor.
    void setBlocked(bool blocked);
    void setKeyboardFocused(bool focused);
    void focusPassword();
    // Brings out the password field the reader's prompt holds back; any key
    // asks for it.
    void revealPassword();

    // In panel coordinates; it moves with the shake.
    base::RectF fieldBounds() const;
    double fieldOpacity() const;
    // Seconds since the epoch.
    void setClockForTesting(std::time_t now);

protected:
    void layout() override;
    void childrenLaidOut() override;

private:
    void tick();
    void retranslate();
    void updateFingerprint();
    void closeMenus(ui::PointF position);
    bool canSwitchUser() const;
    void updateSwappable();
    void setPickerOpen(bool open);
    void updatePicker();

    ui::Window *m_window;
    AuthController *m_controller;
    base::Timer m_clockTimer;
    std::vector<base::Subscription> m_subscriptions;
    std::optional<std::time_t> m_fixedClock;
    bool m_blocked = false;
    RaisedLabel *m_date = nullptr;
    RaisedLabel *m_time = nullptr;
    ui::ColumnLayout *m_pod = nullptr;
    UserView *m_user = nullptr;
    UserNameField *m_userName = nullptr;
    PasswordField *m_password = nullptr;
    HintLabel *m_fingerprint = nullptr;
    bool m_passwordRevealed = false;
    UnlockPrompt *m_unlock = nullptr;
    ErrorLabel *m_error = nullptr;
    ChoiceMenu *m_sessions = nullptr;
    UserGrid *m_picker = nullptr;
    std::unique_ptr<ui::Animation> m_pickerFade;
    bool m_pickerOpen = false;
    double m_pickerProgress = 0;
    double m_pickerFrom = 0;
};

} // namespace cutefish::auth

#endif // CUTEFISH_AUTH_AUTH_VIEW_H
