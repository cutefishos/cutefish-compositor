// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "base/event_loop.h"
#include "ui/application.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/subsurface.h"
#include "ui/view.h"
#include "ui/window.h"

using namespace cutefish::ui;

// With --subsurface a subsurface above the window, 20 px inside its edges,
// is drawn green by a thread of its own at each size it is given.
class GreenSurface
{
public:
    explicit GreenSurface(Window *window)
        : m_surface(window, Subsurface::Stacking::Above)
    {
        std::fprintf(stderr, "SUBSURFACE valid=%d\n", int(m_surface.isValid()));
        if (!m_surface.isValid())
            return;
        m_surface.onResize = [this](cutefish::base::Size) {
            {
                std::lock_guard lock(m_mutex);
                m_dirty = true;
            }
            m_wake.notify_all();
        };
        m_thread = std::thread([this] { run(); });
    }

    ~GreenSurface()
    {
        if (!m_thread.joinable())
            return;
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
        m_thread.join();
    }

    void setGeometry(const RectF &rect) { m_surface.setGeometry(rect); }

private:
    void run()
    {
        using Clear = void (*)(unsigned);
        using ClearColor = void (*)(float, float, float, float);
        const auto clear = reinterpret_cast<Clear>(Subsurface::getProcAddress("glClear"));
        const auto clearColor = reinterpret_cast<ClearColor>(Subsurface::getProcAddress("glClearColor"));
        for (;;) {
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] { return m_stopping || m_dirty; });
                if (m_stopping)
                    break;
                m_dirty = false;
            }
            const cutefish::base::Size size = m_surface.beginFrame();
            if (size.isEmpty())
                continue;
            clearColor(0, 1, 0, 1);
            clear(0x4000);
            m_surface.endFrame();
        }
        m_surface.doneCurrent();
    }

    Subsurface m_surface;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_dirty = false;
    bool m_stopping = false;
};

// With --scroll a list scrolls by 9 px every 50 ms, so every frame carries a small damage region.
class Content : public View
{
public:
    explicit Content(bool scrolling)
        : m_scrolling(scrolling)
        , m_size(addChild<Label>())
    {
        m_size->setTextAlignment(Label::TextAlignment::Center);
    }

    void setGreen(GreenSurface *green)
    {
        m_green = green;
        requestLayout();
    }

    void scrollBy(double distance)
    {
        m_contentY = std::fmod(m_contentY + distance, 1000);
        requestPaint();
    }

protected:
    void layout() override
    {
        const RectF bounds = localRect();
        m_size->setText(std::to_string(int(bounds.width)) + " x " + std::to_string(int(bounds.height)));
        const SizeF hint = m_size->sizeHint();
        m_size->setGeometry({(bounds.width - hint.width) / 2, (bounds.height - hint.height) / 2,
                             hint.width, hint.height});
        if (m_green)
            m_green->setGeometry({20, 20, bounds.width - 40, bounds.height - 40});
    }

    void paint(Painter &painter) override
    {
        painter.fillRect(localRect(), Color::rgb(0xFFFFFF));
        if (!m_scrolling)
            return;
        const RectF list(10, 60, localRect().width - 20, localRect().height - 70);
        painter.save();
        painter.clip(list);
        for (int row = 0; row < 50; ++row)
            painter.fillRect({list.x, list.y + row * 40 - m_contentY, list.width, 20},
                             Color::rgb(0x2468AC));
        painter.restore();
    }

private:
    bool m_scrolling;
    Label *m_size;
    GreenSurface *m_green = nullptr;
    double m_contentY = 0;
};

int main(int argc, char **argv)
{
    Application application(argc, argv);
    bool scrolling = false, subsurface = false;
    for (const std::string &argument : application.arguments()) {
        if (argument == "--scroll")
            scrolling = true;
        if (argument == "--subsurface")
            subsurface = true;
    }

    Window window;
    window.setTitle("toolkit-resize-test");
    window.resize({480, 360});
    auto content = std::make_unique<Content>(scrolling);
    Content *view = content.get();
    window.setContentView(std::move(content));
    window.show();
    std::unique_ptr<GreenSurface> green;
    if (subsurface) {
        green = std::make_unique<GreenSurface>(&window);
        view->setGreen(green.get());
    }

    if (scrolling)
        application.loop().startTimer(std::chrono::milliseconds(50), true, [view] { view->scrollBy(9); });
    application.loop().startTimer(std::chrono::seconds(15), false, [&application] { application.quit(); });
    return application.run();
}
