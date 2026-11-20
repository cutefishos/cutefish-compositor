// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_CENTRE_H_
#define CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_CENTRE_H_

#include <functional>
#include <optional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "base/timer.h"
#include "shell/notification/notification.h"
#include "shell/notification/notification_motion.h"
#include "ui/view.h"
#include "ui/window.h"

namespace cutefish::ui {
class Label;
class ScrollView;
} // namespace cutefish::ui

namespace cutefish::shell {

class ClearButton;
class PillButton;
class NotificationBanners;
class NotificationCard;
class NotificationHistory;
class Shell;
class ShellWindow;

class NotificationCentre
{
public:
    static constexpr int kWidth = 380;
    static constexpr int kMargin = 6;
    static constexpr double kRadius = 18;
    static constexpr double kInset = 12;
    static constexpr double kHeaderHeight = 32;
    static constexpr double kPeekStep = 8;
    static constexpr double kGroupHeaderHeight = 26;
    static constexpr std::chrono::milliseconds kDismissFade {200};
    static constexpr std::chrono::milliseconds kRemovalDelay {650};

    class Row;
    class GroupHeader;

    NotificationCentre(Shell *shell, NotificationHistory &history, NotificationBanners &banners);
    ~NotificationCentre();

    NotificationCentre(const NotificationCentre &) = delete;
    NotificationCentre &operator=(const NotificationCentre &) = delete;

    // `position` is in global logical coordinates.
    void open(ShellWindow *parent, const base::Point &position);
    void close();
    void toggle(ShellWindow *parent, const base::Point &position);
    bool isOpen() const { return m_open; }
    base::Signal<> openChanged;

    // Global logical, while open.
    base::Rect geometry() const { return m_geometry; }
    ShellWindow *shellWindow() const;
    ClearButton *clearButton() const { return m_clear; }
    const std::vector<Row *> &rows() const { return m_rows; }
    const std::vector<GroupHeader *> &groupHeaders() const { return m_headers; }
    int liveCount() const { return m_liveCount; }
    bool isExpanded(const std::string &groupKey) const { return m_expanded.count(groupKey) > 0; }
    bool isInMotion() const { return m_motion; }

    void relayout();

private:
    class Content;
    class List;

    struct Placement {
        ShellWindow *parent = nullptr;
        base::Rect geometry;
    };

    Placement placementFor(ShellWindow *parent, const base::Point &position) const;
    void build(const Placement &placement);
    void destroyWindow();
    void applyGlass();
    void onHidden();
    void retranslate();

    void insertRows(int first, int count);
    void removeRows(int first, int count);
    void moveRow(int from, int to);
    void updateRows(int first, int last);
    void resetRows();
    Row *makeRow(int index);

    void scheduleLayout();
    void layoutPass();
    GroupHeader *headerFor(const std::string &key);
    void pruneHeaders();
    void setExpanded(const std::string &key, bool expanded);
    void beginMotion(std::chrono::milliseconds duration = std::chrono::milliseconds(410));
    void dismissRows(const std::function<bool(const Row &)> &predicate);
    void dismissGroup(const std::string &key);
    void toggleDetail(Row *row);
    void commitRemovals();
    void disarmClears(std::optional<ui::PointF> windowPoint);

    Shell *m_shell;
    NotificationHistory &m_history;
    NotificationBanners &m_banners;

    std::unique_ptr<ui::Window> m_window;
    ShellWindow *m_parent = nullptr;
    base::Rect m_geometry;
    base::Rect m_placedGeometry;
    bool m_open = false;
    Content *m_content = nullptr;
    ui::Label *m_title = nullptr;
    ClearButton *m_clear = nullptr;
    ui::ScrollView *m_scroll = nullptr;
    List *m_list = nullptr;
    ui::Label *m_empty = nullptr;

    std::vector<Row *> m_rows;
    std::vector<GroupHeader *> m_headers;
    std::set<std::string> m_expanded;
    int m_liveCount = 0;
    bool m_motion = false;
    bool m_inLayout = false;
    bool m_layoutDirty = false;
    bool m_layoutScheduled = false;

    base::Timer m_motionTimer;
    base::Timer m_removalTimer;
    base::Timer m_timeTimer;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::vector<base::Subscription> m_subscriptions;
};

class NotificationCentre::Row : public ui::View
{
public:
    Row(NotificationCentre *centre, ui::Window *window);
    ~Row() override;

    void setNotification(const Notification &notification, const std::string &timeText);
    const Notification &notification() const { return m_notification; }
    const std::string &groupKey() const { return m_groupKey; }
    NotificationCard *card() const { return m_card; }

    bool placed = false;
    bool lead = true;
    bool groupExpanded = false;
    int stackCount = 0;
    bool dismissing = false;
    bool gone = false;

    bool isStacked() const { return lead && !groupExpanded && stackCount > 0; }
    bool isShown() const { return !dismissing && (lead || groupExpanded); }
    bool isOccupying() const { return (lead || groupExpanded) && (!dismissing || !gone); }
    int peekCount() const;
    double contentHeight() const;
    double targetHeight() const;
    double targetY() const { return m_y.target(); }
    double contentOpacity() const { return m_opacity.value(); }
    double frameHeight() const { return m_frameHeight.value(); }
    double peekOpacity(int index) const { return m_peeks[index]->value(); }

    void setDetailed(bool detailed);
    bool isDetailed() const;
    void moveTo(double y, bool animate);
    bool isSeenWithin(double top, double bottom, double y) const;
    void apply(bool animated);
    void startGoneTimer();
    void disarmUnless(ui::PointF windowPoint);
    void place();

private:
    class Content;

    void placeContent();

    NotificationCentre *m_centre;
    Notification m_notification;
    std::string m_groupKey;
    Content *m_content = nullptr;
    ui::View *m_frame = nullptr;
    NotificationCard *m_card = nullptr;
    Tween m_y;
    Tween m_contentY;
    Tween m_opacity;
    Tween m_scale;
    Tween m_frameHeight;
    std::unique_ptr<Tween> m_peeks[2];
    bool m_bottomOrigin = true;
    base::Timer m_goneTimer;
};

class NotificationCentre::GroupHeader : public ui::View
{
public:
    GroupHeader(NotificationCentre *centre, ui::Window *window, std::string groupKey);
    ~GroupHeader() override;

    const std::string &groupKey() const { return m_groupKey; }
    void setAppName(const std::string &name);
    void retranslate();
    bool placed = false;
    bool isShown() const { return m_shown; }
    void setShown(bool shown);
    double targetHeight() const;
    void moveTo(double y, bool animate);
    void disarmUnless(ui::PointF windowPoint);
    PillButton *showLessButton() const { return m_showLess; }

protected:
    void layout() override;

private:
    void place();

    NotificationCentre *m_centre;
    std::string m_groupKey;
    bool m_shown = false;
    ui::Label *m_label = nullptr;
    PillButton *m_showLess = nullptr;
    ClearButton *m_clear = nullptr;
    Tween m_y;
    Tween m_opacity;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_NOTIFICATION_NOTIFICATION_CENTRE_H_
