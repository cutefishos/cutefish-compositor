// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ui/wm/core/window_modality_controller.{h,cc}
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WM_WINDOW_MODALITY_CONTROLLER_H_
#define CUTEFISH_WM_WINDOW_MODALITY_CONTROLLER_H_

namespace cutefish::wm {

class Window;
class WindowManager;

bool isModalInEffect(const Window *window);

// Null when none; the innermost blocking modal is returned.
Window *modalTransient(Window *window);
const Window *modalTransient(const Window *window);

class WindowModalityController
{
public:
    explicit WindowModalityController(WindowManager *manager);
    WindowModalityController(const WindowModalityController &) = delete;
    WindowModalityController &operator=(const WindowModalityController &) = delete;

    // Returns true when a modal transient blocks the event, which then must not reach `target`.
    bool processLocatedEvent(Window *target, bool pressed);

    void updateModality(Window *window, bool modalChanged);

private:
    WindowManager *m_manager = nullptr;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WINDOW_MODALITY_CONTROLLER_H_
