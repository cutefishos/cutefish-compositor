// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_CARD_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_CARD_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "shell/notification/notification.h"
#include "ui/label.h"
#include "ui/view.h"

namespace cutefish::ui {
class ImageView;
class ThemeIcon;
} // namespace cutefish::ui

namespace cutefish::shell {

class ClearButton;
class Tween;

class NotificationCard : public ui::View
{
public:
    static constexpr double kPadding = 12;
    static constexpr double kHeaderHeight = 20;
    static constexpr double kIconSize = 16;
    static constexpr double kThumbnailSize = 44;
    static constexpr double kActionHeight = 30;
    static constexpr int kMaxActions = 3;

    explicit NotificationCard(ui::Window *window);
    ~NotificationCard() override;

    void setNotification(const Notification &notification);
    const Notification &notification() const { return m_notification; }
    // Empty shows none.
    void setTimeText(const std::string &text);
    void setMoreText(const std::string &text);
    void setDetailed(bool detailed);
    bool isDetailed() const { return m_detailed; }
    void setConfirmClose(bool confirm);
    void setSurfaceColor(ui::Color color);

    // Button fill for rest (0), hover (1) and press (2).
    ui::Color controlTint(int state) const;
    void disarmUnless(ui::PointF windowPoint);
    ClearButton *closeButton() const { return m_close; }
    // In the card's coordinates.
    std::vector<ui::RectF> actionRects() const;

    double implicitHeight(double width) const;
    double heightForWidth(double width) const override { return implicitHeight(width); }
    bool showsThumbnail() const;

    std::function<void()> onClicked;
    std::function<void()> onCloseClicked;
    std::function<void(const std::string &key)> onActionClicked;
    std::function<void()> onHoveredChanged;
    std::function<void()> onImplicitHeightChanged;

    bool acceptsPointer() const override { return true; }

protected:
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;
    void hoverChanged() override;
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    class ActionButton;

    struct Geometry {
        double headerWidth = 0;
        double closeX = 0;
        double closeY = 0;
        double contentWidth = 0;
        double titleY = -1;
        double bodyY = -1;
        double bodyHeight = 0;
        double progressY = -1;
        double moreY = -1;
        double contentBottom = 0;
        double thumbnailX = 0;
        double thumbnailY = 0;
        double actionsY = -1;
        double height = 0;
    };

    Geometry computeGeometry(double width) const;
    void updateClose();
    void heightMayHaveChanged();
    void updateBody();
    ui::Color secondaryTextColor() const;

    Notification m_notification;
    std::string m_timeText;
    std::string m_moreText;
    bool m_detailed = false;
    bool m_pressed = false;
    ui::Color m_surface;
    bool m_thumbnailFailed = false;
    bool m_thumbnailReady = false;

    ui::ThemeIcon *m_themeIcon = nullptr;
    ui::ImageView *m_fileIcon = nullptr;
    ui::Label *m_appName = nullptr;
    ui::Label *m_when = nullptr;
    ClearButton *m_close = nullptr;
    ui::Label *m_title = nullptr;
    ui::Label *m_body = nullptr;
    ui::Label *m_more = nullptr;
    ui::ImageView *m_thumbnail = nullptr;
    std::vector<ActionButton *> m_actions;
    std::unique_ptr<Tween> m_progress;
    double m_lastHeight = -1;
    std::string m_bodyText;
    uint64_t m_bodyTheme = 0;
};

// The body markup subset: <b>, <strong>, <i>, <em>, <u>, <a href>, <br>, <p>
// and character references; other tags are dropped.
std::vector<ui::Label::Span> styledTextSpans(const std::string &markup, ui::Color linkColor);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_CARD_H_
