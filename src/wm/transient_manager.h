// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modelled on chromium/src/ui/wm/core/transient_window_manager.*
// (revision 7acd40ddefff49c9d36144999aafdbb674dbd07a).

#ifndef CUTEFISH_WM_TRANSIENT_MANAGER_H_
#define CUTEFISH_WM_TRANSIENT_MANAGER_H_

namespace cutefish::wm {

class Window;

class TransientManager
{
public:
    TransientManager() = default;
    TransientManager(const TransientManager &) = delete;
    TransientManager &operator=(const TransientManager &) = delete;

    // Null `parent` clears it. Returns false, changing nothing, on a null child or a cycle.
    bool setTransientParent(Window *child, Window *parent);

    // Transient children are not destroyed with their parent: the client owns their lifetime.
    void windowDestroying(Window *window);

private:
    static bool wouldCreateCycle(const Window *child, const Window *parent);
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_TRANSIENT_MANAGER_H_
