// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/pointer_constraint_delegate.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_POINTER_CONSTRAINT_DELEGATE_H_
#define CUTEFISH_INPUT_POINTER_CONSTRAINT_DELEGATE_H_

#include <optional>


#include "base/geometry.h"
#include "base/region.h"

namespace cutefish::compositor {
class Surface;
}

namespace cutefish::input {

class PointerConstraintDelegate
{
public:
    enum class Kind {
        Lock,
        Confine,
    };

    virtual Kind kind() const = 0;

    // For a persistent constraint, may be called again after onConstraintBroken().
    virtual void onConstraintActivated() = 0;

    virtual void onAlreadyConstrained() = 0;

    virtual void onConstraintBroken() = 0;

    virtual void onDefunct() = 0;

    virtual bool isPersistent() const = 0;

    // Null once the surface is destroyed.
    virtual compositor::Surface* constrainedSurface() const = 0;

    // In logical surface coordinates; unset means the whole input region, and either way it is clipped to it.
    virtual std::optional<base::Region> region() const = 0;

    // In logical surface coordinates.
    virtual std::optional<base::PointF> cursorPositionHint() const = 0;

protected:
    virtual ~PointerConstraintDelegate() = default;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_POINTER_CONSTRAINT_DELEGATE_H_
