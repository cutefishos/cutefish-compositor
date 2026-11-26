// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cursor/cursor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <string>
#include <utility>

#include "compositor/surface.h"
#include "input/seat.h"
#include "input/types.h"

namespace cutefish::cursor {

class Cursor::ImageNode final : public scene::Node
{
public:
    explicit ImageNode(int size)
        : scene::Node("system-cursor")
        , m_size(size)
    {
        m_timer.setSingleShot(true);
        m_timer.onTimeout = [this] {
            m_frame = (m_frame + 1) % m_frames.size();
            if (onFrameChanged)
                onFrameChanged();
            else
                notifyChanged();
            startTimer();
        };
    }

    void setDensity(int density)
    {
        if (density == m_density)
            return;
        m_density = density;
        m_timer.stop();
        m_frames.clear();
    }

    int density() const { return m_density; }

    bool setTheme(const std::string &theme, int size)
    {
        if (m_theme == theme && m_size == size)
            return false;
        m_theme = theme;
        m_size = size;
        m_timer.stop();
        m_frames.clear();
        m_loaded.clear();
        return true;
    }

    void setPointerPosition(const base::PointF &position)
    {
        const base::Point integral = position.toPoint();
        const base::PointF remainder = position - base::PointF(integral);
        if (!input::fuzzyEqual(remainder, m_fractionalOffset)) {
            m_fractionalOffset = remainder;
            notifyChanged();
        }
        setPosition(integral - hotspot());
    }

    void setShape(input::CursorShape shape, bool visible)
    {
        if (shape != m_shape || m_frames.empty()) {
            m_timer.stop();
            m_shape = shape;
            m_frames = frames(shape);
            m_frame = 0;
            if (!m_frames.empty())
                setGeometry(base::Rect(
                        position(),
                        (base::SizeF(m_frames.front().image.size()) / m_density).toSize()));
            notifyChanged();
        }
        m_wantsDisplay = visible;
        setVisible(visible && !m_frames.empty());
        if (!isVisible())
            m_timer.stop();
        else if (m_frames.size() > 1 && !m_timer.isActive())
            startTimer();
    }

    base::Point hotspot() const
    {
        return m_frames.empty() ? base::Point()
                                : (base::PointF(m_frames.front().hotspot) / m_density).toPoint();
    }

    const Frame *currentFrame() const
    {
        return m_frame < m_frames.size() ? &m_frames[m_frame] : nullptr;
    }

    std::function<void()> onFrameChanged;

    bool wantsDisplay() const { return m_wantsDisplay && !m_frames.empty(); }

    Content content() const override
    {
        return m_frames.empty() ? Content::None : Content::Translucent;
    }

protected:
    void paint(render::Renderer &renderer, const base::Rect &geometry) const override
    {
        if (m_frames.empty())
            return;
        const std::string key = "cursor:" + m_theme + ":" + resourceName(m_shape) + ":"
                + std::to_string(m_size * m_density);
        const auto &textures = renderer.cachedTextureSet(key, [&] {
            render::Renderer::TextureSet result;
            for (const auto &frame : m_frames) {
                const auto image = frame.image.converted(ui::Bitmap::Format::Rgba8888Premultiplied);
                result.push_back(render::labelled(renderer.createTexture(image.size(),
                                                                         image.constBits(), int(image.stride()),
                                                                         render::PixelFormat::Rgba8888),
                                                  "cursor"));
            }
            return result;
        });
        if (m_frame < textures.size() && textures[m_frame])
            renderer.drawTextureF(textures[m_frame].get(),
                                  base::RectF(geometry).translated(m_fractionalOffset));
    }

private:
    void startTimer() { m_timer.start(std::chrono::milliseconds(m_frames[m_frame].delayMs)); }

    std::vector<Frame> frames(input::CursorShape shape)
    {
        const int pixels = m_size * m_density;
        auto &loaded = m_loaded[{shape, pixels}];
        if (loaded.empty())
            loaded = load(shape, pixels, m_theme);
        return loaded;
    }

    int m_size;
    std::string m_theme = "default";
    std::map<std::pair<input::CursorShape, int>, std::vector<Frame>> m_loaded;
    int m_density = 1;
    base::PointF m_fractionalOffset;
    input::CursorShape m_shape = input::CursorShape::Hidden;
    std::vector<Frame> m_frames;
    size_t m_frame = 0;
    bool m_wantsDisplay = false;
    base::Timer m_timer;
};

Cursor::Cursor(input::Seat &seat, scene::Node &container, int size)
    : m_seat(seat)
    , m_container(container)
    , m_node(static_cast<ImageNode *>(container.addChild(std::make_unique<ImageNode>(size))))
{
    m_seat.addObserver(this);
    m_clientFrameTimer.setSingleShot(true);
    m_clientFrameTimer.onTimeout = [this] {
        auto *surface = m_clientImage ? m_seat.clientCursorSurface() : nullptr;
        if (!surface || !surface->committedFrameCallbackCount())
            return;
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        surface->frameSubmitted();
        surface->frameDrawn(static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
    };
    m_node->onFrameChanged = [this] {
        if (!m_hardware || !pushToSink())
            updatePresentation();
    };
    onCursorChanged();
}

Cursor::~Cursor()
{
    m_seat.removeObserver(this);
    m_container.removeChild(m_node);
}

base::Point Cursor::deviceHotspot() const
{
    const Frame *frame = m_node->currentFrame();
    if (!frame)
        return {};
    const double ratio = m_scale / m_node->density();
    return base::Point(int(std::floor(frame->hotspot.x() * ratio)),
                       int(std::floor(frame->hotspot.y() * ratio)));
}

void Cursor::setHeld(bool held)
{
    if (m_held == held)
        return;
    m_held = held;
    // The sink needs the position before the image, so it shows on the right output first.
    if (!held && m_sink)
        m_sink->moveCursor(m_seat.cursorPosition());
    updatePresentation();
    onCursorPositionChanged();
}

base::RectF Cursor::captureBounds() const
{
    if (m_clientImage) {
        return base::RectF(m_seat.cursorPosition() - base::PointF(m_clientImage->hotspot),
                           base::SizeF(m_clientImage->logicalSize));
    }
    if (!m_node->wantsDisplay())
        return {};
    const auto *frame = m_node->currentFrame();
    return base::RectF(m_seat.cursorPosition() - base::PointF(frame->hotspot) / m_node->density(),
                       base::SizeF(frame->image.size()) / m_node->density());
}

void Cursor::paintForCapture(render::Renderer &renderer) const
{
    if (m_clientImage) {
        const auto &image = m_clientImage->pixels;
        auto texture = renderer.createTexture(image.size(), image.constBits(),
                                              static_cast<int>(image.stride()),
                                              render::PixelFormat::Bgra8888);
        if (texture)
            renderer.drawTextureF(texture.get(), captureBounds());
        return;
    }
    if (!m_node->wantsDisplay())
        return;
    const auto *frame = m_node->currentFrame();
    const auto image = frame->image.converted(ui::Bitmap::Format::Rgba8888Premultiplied);
    auto texture = renderer.createTexture(image.size(), image.constBits(),
                                          int(image.stride()), render::PixelFormat::Rgba8888);
    if (texture)
        renderer.drawTextureF(
                texture.get(),
                base::RectF(m_seat.cursorPosition() - base::PointF(frame->hotspot) / m_node->density(),
                            base::SizeF(image.size()) / m_node->density()));
}

void Cursor::setScale(double scale)
{
    if (m_scale == scale)
        return;
    m_scale = scale;
    m_node->setDensity(int(std::ceil(scale)));
    onCursorChanged();
}

void Cursor::setTheme(const std::string &theme, int size)
{
    m_theme = validThemeName(theme) ? theme : "default";
    m_size = std::clamp(size, 16, 64);
    // Preferences reloads on any config write; reprogramming the plane blinks the cursor.
    if (!m_node->setTheme(m_theme, m_size))
        return;
    onCursorChanged();
    if (themeChanged)
        themeChanged();
}

void Cursor::setSink(render::CursorSink *sink)
{
    if (m_sink == sink)
        return;
    if (m_sink && m_hardware)
        m_sink->hideCursor();
    m_sink = sink;
    m_hardware = false;
    updatePresentation();
    onCursorPositionChanged();
}

bool Cursor::pushToSink()
{
    const Frame *frame = m_node->currentFrame();
    if (!m_sink || !frame)
        return false;

    // A fractional scale uses the plane too, with the next larger artwork scaled down.
    ui::Bitmap image = frame->image.converted(ui::Bitmap::Format::Argb32Premultiplied);
    const double ratio = m_scale / m_node->density();
    const base::Size deviceSize(int(std::floor(image.width() * ratio)),
                                int(std::floor(image.height() * ratio)));
    if (deviceSize.isEmpty())
        return false;
    if (deviceSize != image.size())
        image = image.scaled(deviceSize);
    render::CursorImage cursorImage;
    cursorImage.pixels = image.constBits();
    cursorImage.size = image.size();
    cursorImage.stride = static_cast<int>(image.stride());
    cursorImage.hotspot = deviceHotspot();
    return m_sink->setCursorImage(cursorImage);
}

bool Cursor::pushClientSurfaceToSink(compositor::Surface *surface, const base::Point &hotspot)
{
    if (!m_sink || !surface)
        return false;
    if (surface->subSurfaceStack().size() > 1
        || surface->bufferTransform() != compositor::Transform::Normal
        || surface->crop().has_value()) {
        return false;
    }
    const base::Size logicalSize = surface->contentSize();
    if (logicalSize.isEmpty())
        return false;

    ui::Bitmap image = surface->copyCursorImage();
    if (image.isNull())
        return false;

    const base::Size deviceSize(std::max(1, int(std::lround(logicalSize.width() * m_scale))),
                                std::max(1, int(std::lround(logicalSize.height() * m_scale))));
    if (deviceSize != image.size())
        image = image.scaled(deviceSize);
    if (image.isNull())
        return false;

    render::CursorImage cursorImage;
    cursorImage.pixels = image.constBits();
    cursorImage.size = image.size();
    cursorImage.stride = static_cast<int>(image.stride());
    cursorImage.hotspot = base::Point(int(std::floor(hotspot.x() * m_scale)),
                                      int(std::floor(hotspot.y() * m_scale)));
    if (!m_sink->setCursorImage(cursorImage))
        return false;

    m_clientImage = ClientImage {std::move(image), logicalSize, hotspot};
    return true;
}

void Cursor::updatePresentation()
{
    m_clientImage.reset();
    if (m_held) {
        m_hardware = false;
        m_clientFrameTimer.stop();
        m_node->setVisible(false);
        return;
    }
    const bool touched = m_seat.cursorHiddenByTouch();
    const bool override = m_seat.cursorOverride().has_value();
    const bool clientShape = m_seat.clientCursorShape().has_value();
    const bool wantsSystemHardware = m_sink && m_node->wantsDisplay();
    if (wantsSystemHardware) {
        m_hardware = pushToSink();
    } else if (m_sink && !touched && !override && !clientShape && m_seat.hasClientCursorRequest()) {
        base::Point clientHotspot;
        compositor::Surface *clientSurface = m_seat.clientCursorSurface(&clientHotspot);
        m_hardware = pushClientSurfaceToSink(clientSurface, clientHotspot);
    } else {
        m_hardware = false;
    }

    if (!m_clientImage) {
        m_clientFrameTimer.stop();
    } else if (!m_clientFrameTimer.isActive()) {
        const auto *surface = m_seat.clientCursorSurface();
        // Hardware cursor animation doesn't draw a scene frame. Give clients
        // at most one frame callback batch per refresh of the cursor's output.
        if (surface && surface->committedFrameCallbackCount())
            m_clientFrameTimer.start(m_sink->frameInterval());
    }
    if (auto *container = m_seat.cursorContainer())
        container->setVisible(!override && !touched && !m_clientImage);

    if (m_sink && !m_hardware)
        m_sink->hideCursor();
    m_node->setVisible(m_node->wantsDisplay() && !m_hardware);
}

void Cursor::refresh()
{
    // Drop the plane first: pushToSink() is skipped while the image is
    // unchanged, and the point here is to reprogram it regardless.
    if (m_sink && !m_held)
        m_sink->hideCursor();
    m_hardware = false;
    onCursorChanged();
}

void Cursor::onCursorChanged()
{
    const auto override = m_seat.cursorOverride();
    const auto client = m_seat.clientCursorShape();
    const bool touched = m_seat.cursorHiddenByTouch();
    const bool visible = !touched && (override || client || !m_seat.hasClientCursorRequest());
    m_node->setShape(override.value_or(client.value_or(m_seat.compositorCursorShape())), visible);
    updatePresentation();
    onCursorPositionChanged();
}

void Cursor::onCursorPositionChanged()
{
    if (m_hardware) {
        // The sink maps the logical position onto each output itself.
        m_sink->moveCursor(m_seat.cursorPosition());
        return;
    }
    m_node->setPointerPosition(m_seat.cursorPosition());
}

} // namespace cutefish::cursor
