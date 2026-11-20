// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_STATUS_ITEMS_VIEW_H_
#define CUTEFISH_SHELL_MENUBAR_STATUS_ITEMS_VIEW_H_

#include <functional>
#include <vector>

#include "base/signal.h"
#include "shell/menubar/bar_item.h"
#include "ui/view.h"

namespace cutefish::wm {
class StatusItem;
class StatusItems;
} // namespace cutefish::wm

namespace cutefish::shell {

class StatusItemSlot;

// Items from cutefish_status_item_v1, in the window manager's order, newest
// leftmost.
class StatusItemsView : public ui::View
{
public:
    StatusItemsView(BarContext *bar, wm::StatusItems *items);
    ~StatusItemsView() override;

    int count() const { return int(m_slots.size()); }
    StatusItemSlot *slot(int index) const { return m_slots[size_t(index)]; }
    double preferredWidth() const;

    void barChanged();
    // The row calls it whenever it moved its views.
    void placeItems();

    std::function<void()> onWidthChanged;

protected:
    void layout() override;

private:
    void rebuild();

    BarContext *m_bar;
    wm::StatusItems *m_items;
    std::vector<StatusItemSlot *> m_slots;
    base::Subscription m_changed;
};

class StatusItemSlot : public BarItem
{
public:
    StatusItemSlot(BarContext *bar, wm::StatusItem *item);

    wm::StatusItem *item() const { return m_item; }
    void setItem(wm::StatusItem *item) { m_item = item; }
    // In slot coordinates, on whole logical pixels in the window.
    ui::RectF itemRect() const;

private:
    wm::StatusItem *m_item;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_STATUS_ITEMS_VIEW_H_
