// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_CONTROLCENTER_TILES_H_
#define CUTEFISH_SHELL_CONTROLCENTER_TILES_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "shell/controlcenter/control_center_services.h"
#include "ui/icon.h"
#include "ui/view.h"

namespace cutefish::ui {
class Animation;
class ImageView;
class Label;
} // namespace cutefish::ui

namespace cutefish::shell {

class SvgIcon : public ui::View
{
public:
    void setFile(std::string file);
    void setTint(std::optional<ui::Color> tint);

protected:
    void paint(ui::Painter &painter) override;

private:
    std::string m_file;
    std::optional<ui::Color> m_tint;
};

class DetailChevron : public ui::Icon
{
public:
    static constexpr double kSize = 12;

    DetailChevron();
    ~DetailChevron() override;

    void setShown(bool shown);

private:
    std::unique_ptr<ui::Animation> m_fade;
    double m_from = 0;
    double m_to = 0;
};

class CardItem : public ui::View
{
public:
    static constexpr double kPlateSize = 28;
    static constexpr double kIconSize = 16;
    static constexpr double kHeight = kPlateSize + 12 * 2 + 4;

    CardItem();
    ~CardItem() override;

    void setArtwork(std::string name);
    void setGlyph(std::string name);
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    void setTitle(std::string title);
    void setStatus(std::string status);
    void setHasDetail(bool detail);
    bool hasDetail() const { return m_hasDetail; }
    // In the card's coordinates.
    ui::RectF plateRect() const;
    DetailChevron *chevron() const { return m_chevron; }

    std::function<void()> onClicked;
    std::function<void()> onDetail;

    ui::SizeF sizeHint() const override { return {0, kHeight}; }
    bool acceptsPointer() const override { return true; }

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;
    void hoverChanged() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;

private:
    void updateIcon();
    bool onPlate(const ui::PointF &point) const;

    std::string m_artwork;
    bool m_checked = false;
    bool m_pressed = false;
    bool m_hasDetail = false;
    bool m_platePressed = false;
    SvgIcon *m_svg;
    ui::Icon *m_glyph;
    DetailChevron *m_chevron;
    ui::Label *m_title;
    ui::Label *m_status;
};

class IconButton : public ui::View
{
public:
    IconButton();

    void setFile(std::string file);
    void setSurfaceColor(ui::Color color);

    std::function<void()> onClicked;

    bool acceptsPointer() const override { return true; }

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;
    void hoverChanged() override;
    void enabledChanged() override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerMove(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;

private:
    SvgIcon *m_icon;
    ui::Color m_surface;
    bool m_pressed = false;
    bool m_inside = false;
};

class MediaTile : public ui::View
{
public:
    static constexpr double kHeight = 60;

    MediaTile();

    void setState(const MediaState &state);
    IconButton *previousButton() const { return m_previous; }
    IconButton *playButton() const { return m_play; }
    IconButton *nextButton() const { return m_next; }

    std::function<void()> onPrevious;
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onNext;

    ui::SizeF sizeHint() const override { return {0, kHeight}; }

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    void updateArtwork();

    MediaState m_state;
    SvgIcon *m_cover;
    ui::ImageView *m_art;
    ui::Label *m_title;
    ui::Label *m_artist;
    IconButton *m_previous;
    IconButton *m_play;
    IconButton *m_next;
    bool m_artReady = false;
};

class DeviceUsersTile : public ui::View
{
public:
    static constexpr double kIconSize = 20;

    DeviceUsersTile();

    void setUsers(const std::vector<DeviceUser> &users);
    const std::vector<DeviceUser> &users() const { return m_users; }
    void retranslate();

    double heightForWidth(double width) const override;
    ui::SizeF sizeHint() const override { return {0, heightForWidth(0)}; }

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;

private:
    struct Row {
        ui::View *icon;
        ui::Label *sentence;
        ui::Color dot;
    };

    double sentenceWidth(double width) const;
    double rowHeight(const Row &row, double width) const;
    double inset() const;

    std::vector<DeviceUser> m_users;
    std::vector<Row> m_rows;
};

std::string deviceUsageText(const DeviceUser &user);

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_CONTROLCENTER_TILES_H_
