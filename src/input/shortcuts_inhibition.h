// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_INPUT_SHORTCUTS_INHIBITION_H_
#define CUTEFISH_INPUT_SHORTCUTS_INHIBITION_H_

#include <map>

#include "compositor/surface_observer.h"

namespace cutefish::input {

class Seat;

class ShortcutsInhibitorDelegate
{
public:
    // Null once the surface is destroyed.
    virtual compositor::Surface* inhibitedSurface() const = 0;

    virtual void onInhibitorActivated() = 0;
    virtual void onInhibitorDeactivated() = 0;

protected:
    virtual ~ShortcutsInhibitorDelegate() = default;
};

class ShortcutsInhibition : public compositor::SurfaceObserver
{
public:
    explicit ShortcutsInhibition(Seat* seat);
    ShortcutsInhibition(const ShortcutsInhibition&) = delete;
    ShortcutsInhibition& operator=(const ShortcutsInhibition&) = delete;
    ~ShortcutsInhibition() override;

    // False when the surface already has an inhibitor.
    bool add(ShortcutsInhibitorDelegate* delegate);

    void remove(ShortcutsInhibitorDelegate* delegate);

    bool isActive() const { return m_active; }

    void update();

    // Deactivates the inhibitor until a click on its window permits it again.
    // Returns whether one was active.
    bool breakByUserAction();

    void onPointerPressed(compositor::Surface* surface);

    void onSurfaceDestroying(compositor::Surface* surface) override;

private:
    Seat* m_seat;
    std::map<compositor::Surface*, ShortcutsInhibitorDelegate*> m_inhibitors;
    ShortcutsInhibitorDelegate* m_active = nullptr;
    bool m_permitted = true;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_SHORTCUTS_INHIBITION_H_
