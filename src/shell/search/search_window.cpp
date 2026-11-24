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
#include "base/region.h"
#include "clipboard/selection.h"
#include "clipboard/stored_source.h"
#include "input/seat.h"
#include "shell/common/app_launcher.h"
#include "shell/search/search_view.h"
#include "shell/shell_data.h"
#include "shell/shell_window.h"
#include "ui/embedded_window.h"
#include "ui/frame_client.h"
#include "ui/theme.h"
#include "wm/window_manager.h"

namespace cutefish::shell {

namespace {

const base::LogCategory cfSearch("cutefish.search", base::LogLevel::Warning);
constexpr double kAnimationDurationMs = 200.0;

render::GlassParams searchGlass()
{
    const bool dark = ui::Theme::current().darkMode;
    render::GlassParams glass = dark ? popupGlass(0.14, 0.15, 0.18, 0.50)
                                     : popupGlass(1.0, 1.0, 1.0, 0.46);
    glass.circularCorners = true;
    return glass;
}

} // namespace

class SearchWindow::HeightAnimation final : public ui::FrameClient
{
public:
    explicit HeightAnimation(SearchWindow *owner)
        : FrameClient(owner->m_window->window())
        , m_owner(owner)
    {
    }

    void start()
    {
        if (!isTicking())
            startTicking();
    }

    void stop()
    {
        if (isTicking())
            stopTicking();
    }

protected:
    void tick(uint64_t frameTimeNs) override { m_owner->tickAnimation(frameTimeNs); }

private:
    SearchWindow *m_owner;
};

SearchWindow::SearchWindow(Shell *shell, base::dbus::Connection *bus)
    : m_shell(shell)
    , m_bus(bus)
    , m_model(std::make_unique<SearchModel>())
{
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
    m_heightAnimation = std::make_unique<HeightAnimation>(this);
    m_height = int(std::lround(m_view->preferredHeight()));
    m_view->setPanelHeight(m_height);
    setWindowHeight(SearchView::kMaxHeight);
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
        if (isOpen()) {
            setWindowHeight(SearchView::kMaxHeight);
            place();
        }
    }));
}

SearchWindow::~SearchWindow()
{
    m_subscriptions.clear();
    m_object.reset();
    m_heightAnimation.reset();
    m_window.reset();
    m_model.reset();
}

bool SearchWindow::isOpen() const
{
    return m_window->isShown();
}

base::Rect SearchWindow::geometry(int height) const
{
    const int width = SearchView::kWidth;
    const int requested = height > 0 ? height : int(std::lround(m_view->preferredHeight()));
    const Shell::Output *output = m_shell->output({});
    if (!output)
        return base::Rect(0, 0, width, requested);
    const base::Rect area = m_shell->workArea(*output);
    const int top = area.y() + std::min(96, std::max(0, area.height() - requested));
    const int clampedHeight = std::min(requested, std::max(1, area.y() + area.height() - top));
    return base::Rect(int(std::lround(area.x() + (area.width() - width) / 2.0)), top, width,
                      clampedHeight);
}

void SearchWindow::setWindowHeight(int height)
{
    m_window->setGeometry(geometry(height));
}

void SearchWindow::place()
{
    const int target = geometry(int(std::lround(m_view->preferredHeight()))).height();
    if (isOpen()) {
        // Retarget while a growth is running, or the panel stops at the height
        // the first result wanted.
        if (!m_animating || target != int(std::lround(m_heightTo)))
            animate(target);
    } else {
        m_height = target;
        m_view->setPanelHeight(m_height);
        setWindowHeight(SearchView::kMaxHeight);
        applyGlass();
    }
}

void SearchWindow::animate(int targetHeight)
{
    if (!m_animating && targetHeight == m_height)
        return;
    // A new target while running starts from where the panel is now.
    m_heightFrom = m_height > 0 ? m_height : targetHeight;
    m_heightTo = targetHeight;
    m_animationStartNs = 0;
    m_animating = true;
    m_heightAnimation->start();
    m_window->requestFrame();
}

void SearchWindow::tickAnimation(uint64_t frameTimeNs)
{
    if (!m_animationStartNs)
        m_animationStartNs = frameTimeNs;
    const double elapsedMs = double(frameTimeNs - m_animationStartNs) / 1e6;
    const double progress = std::min(1.0, elapsedMs / kAnimationDurationMs);
    const double t = 1.0 - std::pow(1.0 - progress, 3);
    m_height = int(std::lround(m_heightFrom + (m_heightTo - m_heightFrom) * t));
    m_view->setPanelHeight(m_height);
    if (progress < 1.0)
        return;
    m_height = int(std::lround(m_heightTo));
    m_view->setPanelHeight(m_height);
    m_animating = false;
    m_heightAnimation->stop();
}

void SearchWindow::applyGlass()
{
    const bool dark = ui::Theme::current().darkMode;
    const int height = m_height > 0 ? m_height : geometry(0).height();
    if (int(dark) == m_glassDark && height == m_glassHeight)
        return;
    m_glassDark = int(dark);
    m_glassHeight = height;
    const base::Rect rect(0, 0, SearchView::kWidth, height);
    m_window->setBackgroundBlur(rect, int(SearchView::kCardRadius), searchGlass());
    m_window->setInputRegion(base::Region(rect));
}

void SearchWindow::open()
{
    if (isOpen())
        return;
    m_hadKeyboard = false;
    m_heightAnimation->stop();
    m_animating = false;
    m_view->reset();
    // The field is already at its place on the first frame; the window's own
    // launchpad animation brings the whole surface in.
    m_height = geometry(int(std::lround(m_view->preferredHeight()))).height();
    m_view->setPanelHeight(m_height);
    setWindowHeight(SearchView::kMaxHeight);
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
    m_heightAnimation->stop();
    m_animating = false;
    m_model->stop();
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
