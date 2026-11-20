// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_SHELL_DRAG_H_
#define CUTEFISH_SHELL_SHELL_DRAG_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "base/geometry.h"
#include "base/signal.h"
#include "ui/drag.h"

namespace cutefish::compositor {
class Buffer;
class Surface;
}

namespace cutefish::input {
class DragOffer;
}

namespace cutefish::shell {

class Shell;
class ShellWindow;

// A drag started in a shell window is a data source offered to clients and
// shell windows alike; a drag over a shell window is offered to its views
// through ui::DropOffer.
class ShellDrag
{
public:
    explicit ShellDrag(Shell *shell);
    ShellDrag(const ShellDrag &) = delete;
    ShellDrag &operator=(const ShellDrag &) = delete;
    ~ShellDrag();

    // The drag starts on the next loop turn, out of the input dispatch that
    // asked for it; false when one is running.
    bool start(ShellWindow *window, ui::DragSource source);

    // The window under a drag, or null.
    ShellWindow *target() const;

    void windowDestroying(ShellWindow *window);

private:
    class DataSource;
    class Offer;

    void begin(ShellWindow *window, ui::DragSource source);
    void enter(compositor::Surface *surface, const base::PointF &location);
    void motion(const base::PointF &location);
    void leave();
    void dropped(input::DragOffer *offer);
    void apply(uint32_t answer);

    Shell *m_shell;
    // A surface only refers to its buffer, so the drag owns it.
    std::unique_ptr<compositor::Surface> m_icon;
    std::shared_ptr<compositor::Buffer> m_iconBuffer;
    bool m_starting = false;
    // Dropped offers stay until their reads are done.
    std::shared_ptr<Offer> m_offer;
    std::vector<std::shared_ptr<Offer>> m_dropped;
    std::vector<base::Subscription> m_subscriptions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_SHELL_DRAG_H_
