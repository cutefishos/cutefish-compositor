// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_LAUNCHER_LAUNCHER_PAGE_INDICATOR_H_
#define CUTEFISH_SHELL_LAUNCHER_LAUNCHER_PAGE_INDICATOR_H_

#include <functional>
#include <vector>

#include "shell/launcher/launcher_tween.h"
#include "ui/view.h"

namespace cutefish::shell {

// A row of round dots, the current page's lit; a press or a drag over a dot
// picks its page. It shows only for more than one page.
class LauncherPageIndicator final : public ui::View
{
public:
    // The padding around the dots.
    static constexpr double kPadding = 6;

    LauncherPageIndicator();

    bool acceptsPointer() const override { return m_count > 1; }

    void setDotSize(double size);
    void setDots(int count, int current, bool animate);

    // The indicator's own size, padding included.
    ui::SizeF implicitSize() const;
    ui::RectF dotRect(int index) const;

    std::function<void(int)> onPick;

protected:
    void paint(ui::Painter &painter) override;
    void pointerPress(const ui::PointerEvent &event) override { pick(event.position); }
    void pointerMove(const ui::PointerEvent &event) override { pick(event.position); }

private:
    double spacing() const;
    // The dot under the pointer, its spacing shared with its neighbours.
    void pick(ui::PointF position);

    double m_dotSize = 0;
    int m_count = 0;
    int m_current = -1;
    std::vector<double> m_alphas;
    LauncherTween m_opacityTween;
    LauncherTween m_colorTween;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_LAUNCHER_LAUNCHER_PAGE_INDICATOR_H_
