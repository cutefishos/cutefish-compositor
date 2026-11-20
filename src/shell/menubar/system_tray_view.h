// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_SYSTEM_TRAY_VIEW_H_
#define CUTEFISH_SHELL_MENUBAR_SYSTEM_TRAY_VIEW_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/signal.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "ui/animation.h"
#include "shell/menubar/bar_item.h"
#include "ui/bitmap.h"
#include "ui/image.h"
#include "ui/view.h"

namespace cutefish::ui {
class Menu;
class ThemeIcon;
} // namespace cutefish::ui

namespace cutefish::shell {

class DBusMenu;
class MenuPopup;
class SystemTrayModel;
class TrayItemView;

class SystemTrayView : public ui::View
{
public:
    struct Host {
        std::function<void()> closePanels;
        std::function<void()> closeAppMenus;
        std::function<void()> closePopups;
        std::function<ui::RectF()> availableGeometry;
        std::function<ui::PointF()> origin;
        // Empty keeps the toolkit's.
        std::function<std::string(const std::string &key, uint32_t modifiers)> shortcutText;
    };

    static constexpr double kItemWidth = 40;

    SystemTrayView(BarContext *bar, SystemTrayModel *model, Host host);
    ~SystemTrayView() override;

    int count() const { return int(m_items.size()); }
    TrayItemView *item(int row) const;
    double preferredWidth() const { return kItemWidth * count(); }

    void closeMenu();
    ui::Menu *openMenu() const;
    void barChanged();

    std::function<void()> onCountChanged;

    // Empty for none.
    const std::string &draggedItem() const { return m_dragged; }

    void itemClicked(TrayItemView *item, const ui::PointerEvent &event);
    void startItemDrag(TrayItemView *item);
    void dragEntered(TrayItemView *target);

protected:
    void layout() override;

private:
    void rebuild();
    void toggleMenu(TrayItemView *item, const ui::PointF &position);
    void openContextMenu(TrayItemView *item, const ui::PointF &position);
    void showMenu(TrayItemView *item, DBusMenu *menu);
    void menuClosed();
    double slotX(int row) const;

    BarContext *m_bar;
    SystemTrayModel *m_model;
    Host m_host;
    std::vector<TrayItemView *> m_items;
    std::unique_ptr<MenuPopup> m_popup;
    TrayItemView *m_popupItem = nullptr;
    TrayItemView *m_menuItem = nullptr;
    base::Timer m_menuWait;
    base::Subscription m_menuLoaded;
    std::vector<base::Subscription> m_subscriptions;
    std::string m_dragged;
    std::unique_ptr<ui::Animation> m_moveAnimation;
    std::map<TrayItemView *, std::pair<double, double>> m_moves;
    double m_moveProgress = 1;
    bool m_animateMoves = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

class TrayItemView : public BarItem
{
public:
    TrayItemView(BarContext *bar, SystemTrayView *tray, std::string id);
    ~TrayItemView() override;

    const std::string &itemId() const { return m_id; }

    void setIcon(const std::string &name, const std::vector<ui::Bitmap> &pixmaps,
                 const std::string &fallback);
    void updateLook();
    ui::Bitmap dragImage(double scale) const;
    void setIconHidden(bool hidden);

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;
    bool canDrop(const ui::DropOffer &offer) const override;
    void dragEnter(const ui::DragEvent &event) override;
    uint32_t dragUpdate(const ui::DragEvent &event) override;
    uint32_t drop(const ui::DragEvent &event) override;

private:
    void setMonochrome(bool monochrome);
    void updatePixmapImage();

    SystemTrayView *m_tray;
    std::string m_id;
    std::string m_name;
    std::string m_file;
    std::vector<ui::Bitmap> m_pixmaps;
    bool m_monochrome = false;
    ui::ThemeIcon *m_themeIcon = nullptr;
    ui::Image m_image;
    ui::Image m_tinted;
    ui::Color m_tintedColor;
    base::Job m_probe;
    std::string m_probed;
    bool m_iconHidden = false;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_SYSTEM_TRAY_VIEW_H_
