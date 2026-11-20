// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_COMMON_COUNT_BADGE_H_
#define CUTEFISH_SHELL_COMMON_COUNT_BADGE_H_

#include <cstdint>
#include <string>

#include "ui/view.h"

namespace cutefish::ui {
class Label;
} // namespace cutefish::ui

namespace cutefish::shell {

class CountBadge final : public ui::View
{
public:
    static constexpr ui::Color kColor {255, 59, 48, 255};
    static constexpr double kDockRatio = 0.34;
    static constexpr double kLaunchpadRatio = 0.24;

    CountBadge();
    ~CountBadge() override;

    // Hidden at 0.
    void setCount(int64_t count);
    int64_t count() const { return m_count; }
    const std::string &text() const;
    static std::string textFor(int64_t count);

    void setHeightRatio(double ratio);
    // `icon` is in the parent's coordinates.
    void setIconRect(const ui::RectF &icon);
    static double heightFor(double iconSize, double ratio);

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    void place();

    ui::Label *m_label = nullptr;
    int64_t m_count = 0;
    double m_heightRatio = kDockRatio;
    ui::RectF m_iconRect;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_COMMON_COUNT_BADGE_H_
