// Copyright 2019 The Chromium Authors
// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/ash/wm/wm_shadow_controller_delegate.{h,cc}
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_WM_WM_SHADOW_CONTROLLER_DELEGATE_H_
#define CUTEFISH_WM_WM_SHADOW_CONTROLLER_DELEGATE_H_

#include "wm/shadow_controller_delegate.h"

namespace cutefish::wm {

class WmShadowControllerDelegate : public ShadowControllerDelegate
{
public:
    bool shouldShowShadowForWindow(const Window* window) override;
    ShadowElevation elevationForWindow(const Window* window) override;
};

} // namespace cutefish::wm

#endif // CUTEFISH_WM_WM_SHADOW_CONTROLLER_DELEGATE_H_
