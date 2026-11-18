// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

#include "base/region.h"
#include "base/settings.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "shell/spotlight/spotlight.h"
#include "shell/spotlight/spotlight_search.h"
#include "ui/bitmap.h"
#include "ui/menu.h"
#include "ui/scroller.h"
#include "ui/view.h"

namespace cutefish::ui {
class ImageView;
class Path;
}

namespace cutefish::shell {

class SpotlightView : public ui::View, private ui::Scroller::Target
{
public:
    using Mode = Spotlight::Mode;
    static constexpr int kWidth = 620;
    static constexpr int kHeader = 54;

    SpotlightView(Shell &shell, Dock *dock, ClipboardHistory &clipboard, std::string settingsPath);
    ~SpotlightView() override;

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    void reset();
    void focusSearch();
    void setExpansion(double value);
    void stopScrolling();
    bool idle() const;
    int preferredHeight() const;
    ui::Path glassShape(int width, int height) const;
    base::Region inputRegion(int width, int height) const;
    bool menuOpen() const { return m_menu.isOpen(); }
    bool dragging() const { return m_dragging; }
    void closeMenu() { m_menu.close(); }

    std::function<void()> onDismiss;
    std::function<void()> onResize;
    std::function<void()> onPaint;

    bool acceptsPointer() const override { return true; }

protected:
    void layout() override;
    void paint(ui::Painter &painter) override;
    void pointerPress(const ui::PointerEvent &event) override;
    void pointerMove(const ui::PointerEvent &event) override;
    void pointerRelease(const ui::PointerEvent &event) override;
    void hoverMove(const ui::PointerEvent &event) override;
    void pointerLeave() override;
    bool scroll(const ui::ScrollEvent &event) override;

private:
    class Field;
    class WheelAnimation;
    struct Result {
        Mode type;
        std::string title;
        std::string detail;
        std::string icon;
        std::string key;
        DesktopEntryPtr app = {};
        std::vector<std::string> command = {};
        bool folder = false;
        std::string section = {};
        std::string quick = {};
    };
    struct Tile {
        ui::RectF rect;
        int result;
    };
    struct Section {
        double y;
        std::string name;
    };
    struct Chip {
        int value;
        std::string label;
        bool active;
        ui::RectF rect = {};
    };
    struct ResultIcon {
        ui::ImageView *view;
        int result = -1;
        std::string source;
        std::string previewKey;
    };

    bool grid() const { return m_mode == Mode::Applications && !m_list; }
    bool key(const ui::KeyEvent &event, Field *field);
    void refreshResults();
    void refreshLayout();
    void requestFiles();
    void buildTiles(bool grid, double width);
    void appendApps(const std::string &query);
    void appendFiles(const std::string &query);
    void appendActions(const std::string &query);
    void appendClipboard(const std::string &query);
    void select(int row);
    void activate(int row, bool alternate = false);
    void openMenu(ui::PointF point, int row = -1);
    void showAction(const Result &result, bool quick);
    void runAction();
    void exitAction();
    void setFileFolder(std::string folder);
    void switchChip(int value);
    int hitResult(ui::PointF point) const;
    int hitMode(ui::PointF point) const;
    double fieldWidth(double width) const;
    ui::RectF modeButton(double width, int index) const;
    std::string modeModifierLabel() const;
    double bodyTop() const;
    ui::Scroller::Axis horizontalAxis() const override;
    ui::Scroller::Axis verticalAxis() const override;
    ui::PointF position() const override { return {m_chipScroll, m_scroll}; }
    void applyPosition(ui::PointF position) override;
    ui::PointF boundedPosition(ui::PointF position) const;
    void fadeBody();
    void rememberApp(const std::string &id);
    void openTarget(const std::string &target);
    bool updateClipboardPreview(const Result &result, ResultIcon &icon);

    Shell &m_shell;
    Dock *m_dock;
    ClipboardHistory &m_clipboard;
    base::Settings m_settings;
    ui::Menu m_menu;
    Field *m_field;
    Field *m_parameter;
    Field *m_subject;
    Field *m_message;
    ui::View *m_icons;
    std::vector<ResultIcon> m_images;
    std::vector<Result> m_results;
    std::vector<Tile> m_tiles;
    std::vector<Section> m_sections;
    std::vector<Chip> m_chips;
    std::vector<spotlight::File> m_files;
    std::vector<std::string> m_recent;
    std::vector<base::Subscription> m_subscriptions;
    std::unique_ptr<ui::Scroller> m_scroller;
    std::unique_ptr<WheelAnimation> m_wheel;
    std::unique_ptr<ui::Animation> m_bodyFade;
    ui::PointF m_wheelTarget;
    base::Job m_indexJob;
    base::Timer m_queryTimer;
    std::chrono::steady_clock::time_point m_indexed;
    std::optional<Result> m_action;
    std::string m_folder;
    std::string m_status;
    Mode m_mode = Mode::Search;
    spotlight::Category m_category = spotlight::Category::All;
    spotlight::FileType m_fileType = spotlight::FileType::All;
    int m_selected = -1;
    int m_hot = -1;
    int m_hoverMode = -1;
    std::string m_dragPath;
    std::string m_dragIcon;
    ui::PointF m_press;
    double m_bodyHeight = 0;
    double m_tilesWidth = 0;
    double m_chipsWidth = 0;
    double m_expansion = 0;
    double m_bodyOpacity = 1;
    double m_scroll = 0;
    double m_chipScroll = 0;
    bool m_list = false;
    bool m_updating = false;
    bool m_savingQuick = false;
    bool m_confirmClear = false;
    bool m_dragging = false;
    std::unordered_map<std::string, std::optional<ui::Bitmap>> m_thumbnails;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell
