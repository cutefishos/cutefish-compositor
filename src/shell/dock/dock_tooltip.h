// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_DOCK_DOCK_TOOLTIP_H_
#define CUTEFISH_SHELL_DOCK_DOCK_TOOLTIP_H_

#include <memory>
#include <string>

#include "base/geometry.h"
#include "ui/geometry.h"

namespace cutefish::ui {
class Label;
class Window;
} // namespace cutefish::ui

namespace cutefish::shell {

class DockWindow;
class Shell;

class DockTooltip
{
public:
    DockTooltip(DockWindow *dock, Shell *shell);
    ~DockTooltip();

    // `target` is the icon's centre in the dock window's coordinates.
    void show(const std::string &text, const base::PointF &target, double farSide, const ui::PointF &offset = {});
    // `top` is in the dock window's coordinates.
    void showAbove(const std::string &text, const base::PointF &top);
    void hide();
    bool isShown() const;
    const std::string &text() const { return m_text; }
    ui::Window *window() const { return m_window.get(); }

private:
    void place(const std::string &text, const base::PointF &target, double farSide, const ui::PointF &offset,
               bool across, bool contained);

    DockWindow *m_dock;
    Shell *m_shell;
    std::unique_ptr<ui::Window> m_window;
    ui::Label *m_label = nullptr;
    // Measures at the dock window's scale: hinted text is wider at 1.5 than at
    // 1.
    ui::Label *m_measure = nullptr;
    std::string m_text;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_DOCK_DOCK_TOOLTIP_H_
