// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/search/search_window.h"

#include <cmath>
#include <string>

#include "base/application_registry.h"
#include "core/main_loop.h"
#include "base/logging.h"
#include "base/process.h"
#include "shell/common/app_launcher.h"
#include "shell/shell_data.h"
#include "shell/search/search_view.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "ui/theme.h"
#include "wm/window_manager.h"

namespace cutefish::shell {

namespace {

const base::LogCategory cfSearch("cutefish.search", base::LogLevel::Warning);

// The panel's glass: clearer than the Control Center's, with a little more
// colour in the backdrop and a highlight along the rim, as macOS Tahoe reads.
render::GlassParams searchGlass()
{
    const bool dark = ui::Theme::current().darkMode;
    render::GlassParams glass = dark ? popupGlass(0.14, 0.15, 0.18, 0.50)
                                     : popupGlass(1.0, 1.0, 1.0, 0.46);
    glass.blurSigma = 8.0f;
    glass.saturation = 1.35f;
    glass.specularIntensity = 0.55f;
    glass.shininess = 60.0f;
    glass.sheen = 0.35f;
    return glass;
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
    // Spotlight's place: centred, a little below the top edge.
    return base::Rect(int(std::lround(area.x() + (area.width() - width) / 2.0)), area.y() + 96, width,
                      height);
}

void SearchWindow::place()
{
    const int target = int(std::lround(m_view->preferredHeight()));
    if (isOpen()) {
        // Retarget while a growth is running, or the panel stops at the height
        // the first result wanted.
        if (!m_animating || m_hideWhenDone || target != int(std::lround(m_heightTo)))
            animate(target, 1, false);
    } else {
        m_height = target;
        m_window->setGeometry(geometry());
        applyGlass();
    }
}

void SearchWindow::animate(int targetHeight, double targetOpacity, bool hideWhenDone)
{
    // A new target while running starts from where the panel is now.
    m_heightFrom = m_height > 0 ? m_height : targetHeight;
    m_heightTo = targetHeight;
    m_opacityFrom = m_view->opacity();
    m_opacityTo = targetOpacity;
    m_animationProgress = 0;
    m_hideWhenDone = hideWhenDone;
    m_animating = true;
    m_animation.start();
}

void SearchWindow::tickAnimation()
{
    // 150 ms, eased out: quick enough to feel direct.
    m_animationProgress = std::min(1.0, m_animationProgress + 16.0 / 150.0);
    const double t = 1 - std::pow(1 - m_animationProgress, 3);
    m_height = int(std::lround(m_heightFrom + (m_heightTo - m_heightFrom) * t));
    m_view->setOpacity(m_opacityFrom + (m_opacityTo - m_opacityFrom) * t);
    m_window->setGeometry(geometry());
    applyGlass();
    if (m_animationProgress < 1)
        return;
    m_animation.stop();
    m_animating = false;
    if (m_hideWhenDone) {
        m_hideWhenDone = false;
        m_window->hide();
        m_view->setOpacity(1);
    }
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
    m_window->setBackgroundBlur(base::Rect(0, 0, SearchView::kWidth, height),
                                int(SearchView::kCardRadius), searchGlass());
}

void SearchWindow::open()
{
    if (isOpen())
        return;
    m_hadKeyboard = false;
    m_animation.stop();
    m_animating = false;
    m_view->reset();
    // The bar first, then it opens: the field is already at its place.
    m_height = int(std::lround(m_view->preferredHeight()));
    m_view->setOpacity(0);
    m_window->setGeometry(geometry());
    applyGlass();
    // Mapping a picker takes the keyboard grab.
    m_window->show();
    animate(int(std::lround(m_view->preferredHeight())), 1, false);
}

void SearchWindow::close()
{
    m_hadKeyboard = false;
    if (!isOpen())
        return;
    animate(m_height, 0, true);
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
        // Return on a sum copies it, as Spotlight does.
        base::Process::startDetached("wl-copy", {result.title});
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
