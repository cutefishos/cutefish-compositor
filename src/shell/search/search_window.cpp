// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/search/search_window.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "base/application_registry.h"
#include "base/logging.h"
#include "base/process.h"
#include "clipboard/selection.h"
#include "clipboard/stored_source.h"
#include "core/main_loop.h"
#include "input/seat.h"
#include "shell/common/app_launcher.h"
#include "shell/search/search_view.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "ui/path.h"
#include "ui/theme.h"
#include "wm/window_manager.h"

namespace cutefish::shell {

namespace {

const base::LogCategory cfSearch("cutefish.search", base::LogLevel::Warning);

render::GlassParams searchGlass()
{
    const bool dark = ui::Theme::current().darkMode;
    return dark ? popupGlass(0.14, 0.15, 0.18, 0.50)
                : popupGlass(1.0, 1.0, 1.0, 0.46);
}

} // namespace

SearchWindow::SearchWindow(Shell *shell, base::dbus::Connection *bus)
    : m_shell(shell)
    , m_bus(bus)
    , m_model(std::make_unique<SearchModel>())
    , m_animation(core::mainLoop())
{
    m_animation.setInterval(std::chrono::milliseconds(16));
    m_animation.onTimeout = [this] { tickAnimation(); };
    m_window = std::make_unique<ShellWindow>(shell, wm::WindowRole::Picker);
    // The field and the glass open and close as one surface, as the launchpad.
    m_window->setLaunchpadAnimation(true);
    auto view = std::make_unique<SearchView>(*m_model);
    m_view = view.get();
    m_view->onCloseRequested = [this] { close(); };
    m_view->onPainted = [this] { applyGlass(); };
    m_view->onActivated = [this](int index) { activate(index); };
    m_view->onHeightChanged = [this] {
        if (isOpen())
            place();
    };
    m_window->setContentView(std::move(view));
    m_window->setSize(base::Size(SearchView::kWidth, int(std::lround(m_view->preferredHeight()))));
    m_window->onKeyboardFocusChanged = [this](bool focused) {
        if (focused) {
            m_hadKeyboard = true;
        } else if (m_hadKeyboard && isOpen()) {
            m_hadKeyboard = false;
            close();
        }
    };
    m_window->onCloseRequested = [this] { close(); };
    applyGlass();

    m_subscriptions.push_back(m_shell->outputsChanged.connect([this] {
        if (isOpen())
            place();
    }));
}

SearchWindow::~SearchWindow()
{
    m_subscriptions.clear();
    m_object.reset();
    m_window.reset();
    m_model.reset();
}

bool SearchWindow::isOpen() const
{
    return m_window->isShown();
}

base::Rect SearchWindow::geometry() const
{
    const int width = SearchView::kWidth;
    const int height = m_height > 0 ? m_height : int(std::lround(m_view->preferredHeight()));
    const Shell::Output *output = m_shell->output({});
    if (!output)
        return base::Rect(0, 0, width, height);
    const base::Rect area = m_shell->workArea(*output);
    const int top = area.y() + std::min(96, std::max(0, area.height() - height));
    const int clampedHeight = std::min(height, std::max(1, area.y() + area.height() - top));
    return base::Rect(int(std::lround(area.x() + (area.width() - width) / 2.0)), top, width,
                      clampedHeight);
}

void SearchWindow::place()
{
    const int target = int(std::lround(m_view->preferredHeight()));
    if (isOpen()) {
        // Retarget while a growth is running, or the panel stops at the height
        // the first result wanted.
        if (!m_animating || target != int(std::lround(m_heightTo)))
            animate(target);
    } else {
        m_height = target;
        m_window->setGeometry(geometry());
        applyGlass();
    }
}

void SearchWindow::animate(int targetHeight)
{
    // A new target while running starts from where the panel is now.
    m_heightFrom = m_height > 0 ? m_height : targetHeight;
    m_heightTo = targetHeight;
    m_animationProgress = 0;
    m_animating = true;
    m_animation.start();
}

void SearchWindow::tickAnimation()
{
    // 150 ms, eased out: quick enough to feel direct.
    m_animationProgress = std::min(1.0, m_animationProgress + 16.0 / 150.0);
    const double t = 1 - std::pow(1 - m_animationProgress, 3);
    m_height = int(std::lround(m_heightFrom + (m_heightTo - m_heightFrom) * t));
    m_window->setGeometry(geometry());
    applyGlass();
    if (m_animationProgress < 1)
        return;
    m_animation.stop();
    m_animating = false;
}

void SearchWindow::applyGlass()
{
    const bool dark = ui::Theme::current().darkMode;
    // The height the window has this frame, not the one it is heading to.
    const int height = geometry().height();
    // The panel grows a row at a time; a glass laid out for the old height
    // would leave the new rows on nothing.
    if (int(dark) == m_glassDark && height == m_glassHeight)
        return;
    m_glassDark = int(dark);
    m_glassHeight = height;
    // Use an explicit circular rounded-rect path: continuous corners scale
    // radius by 1.5x and clamp X/Y independently, which distorts a pill
    // (radius == height / 2) into a stretched squircle and morphs the top
    // corners as the panel grows.
    ui::Path shape;
    shape.addRoundedRect({0, 0, double(SearchView::kWidth), double(height)},
                         SearchView::kCardRadius, SearchView::kCardRadius);
    m_window->setBackgroundBlur(base::Rect(0, 0, SearchView::kWidth, height),
                                int(SearchView::kCardRadius), searchGlass(), shape);
}

void SearchWindow::open()
{
    if (isOpen())
        return;
    m_hadKeyboard = false;
    m_animation.stop();
    m_animating = false;
    m_view->reset();
    // The field is already at its place on the first frame; the window's own
    // launchpad animation brings the whole surface in.
    m_height = int(std::lround(m_view->preferredHeight()));
    m_window->setGeometry(geometry());
    applyGlass();
    // Mapping a picker takes the keyboard grab.
    m_window->show();
}

void SearchWindow::close()
{
    m_hadKeyboard = false;
    if (!isOpen())
        return;
    // No separate content fade: hiding the window starts its one animation,
    // and the field and the glass leave together.
    m_animation.stop();
    m_animating = false;
    m_window->hide();
}

void SearchWindow::toggle()
{
    if (isOpen())
        close();
    else
        open();
}

void SearchWindow::activate(int index)
{
    const std::vector<SearchResult> &results = m_model->results();
    if (index < 0 || index >= int(results.size()))
        return;
    const SearchResult result = results[size_t(index)];
    close();
    if (result.kind == SearchResult::Kind::Answer) {
        if (input::Seat *seat = m_shell->seat(); seat && seat->selection()) {
            auto source = std::make_shared<clipboard::StoredSource>();
            for (const char *mime : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING",
                                     "STRING", "TEXT"}) {
                source->offer(mime);
                source->payloads[mime] = result.title;
            }
            seat->selection()->setSource(std::move(source));
        }
        return;
    }
    if (result.kind == SearchResult::Kind::Application) {
        if (const auto entry = base::ApplicationRegistry::instance().byId(result.appId))
            m_shell->launcher().launch(*entry);
        return;
    }
    if (!result.path.empty())
        base::Process::startDetached("xdg-open", {result.path});
}

bool SearchWindow::registerService()
{
    if (m_object)
        return true;
    if (!m_bus)
        return false;
    auto object = std::make_unique<base::dbus::Object>(m_bus, "/Search", "org.cutefish.Search");
    object->addMethod("toggle", [this] { toggle(); });
    if (!object->publish()
        || !m_bus->requestName("org.cutefish.Search", base::dbus::Connection::DoNotQueue)) {
        CF_WARNING(cfSearch) << "Cannot own org.cutefish.Search";
        return false;
    }
    m_object = std::move(object);
    return true;
}

} // namespace cutefish::shell
