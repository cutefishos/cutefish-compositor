// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_WINDOW_RENDERER_H_
#define CUTEFISH_SHELL_WINDOW_RENDERER_H_

#include <cstdint>
#include <vector>

#include "compositor/compositor.h"

namespace cutefish::shell {

class ShellWindow;

// Each window's pixels go to its surface as a commit, so they reach the scene
// and its damage tracking like a client's buffer.
class WindowRenderer : public compositor::ContentProducer
{
public:
    explicit WindowRenderer(compositor::Compositor *compositor);
    WindowRenderer(const WindowRenderer &) = delete;
    WindowRenderer &operator=(const WindowRenderer &) = delete;
    ~WindowRenderer() override;

    void scheduleRender(ShellWindow *window);
    void cancel(ShellWindow *window);
    bool isScheduled(const ShellWindow *window) const;

    bool hasPendingContent(const base::Rect &viewport) const override;
    void produceContent(render::Renderer &renderer) override;

    uint64_t passCount() const { return m_passCount; }
    const std::vector<const ShellWindow *> &lastPass() const { return m_lastPass; }

private:
    compositor::Compositor *m_compositor;
    std::vector<ShellWindow *> m_scheduled;
    std::vector<ShellWindow *> m_pass;
    std::vector<const ShellWindow *> m_lastPass;
    uint64_t m_passCount = 0;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_WINDOW_RENDERER_H_
