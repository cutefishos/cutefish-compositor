// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.chromium file.
//
// Upstream: chromium/src/components/exo/pointer.h (pointer constraints)
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a

#ifndef CUTEFISH_INPUT_POINTER_CONSTRAINTS_H_
#define CUTEFISH_INPUT_POINTER_CONSTRAINTS_H_

#include <functional>
#include <map>
#include <optional>


#include "compositor/surface_observer.h"
#include "base/geometry.h"
#include "base/region.h"
#include "base/signal.h"

namespace cutefish::input {

class PointerConstraintDelegate;
class Seat;

// At most one constraint per surface, and at most one active: the one on the
// surface with pointer focus, in the active window, once the pointer is inside
// its region.
class PointerConstraints : public compositor::SurfaceObserver
{
public:
    explicit PointerConstraints(Seat *seat);
    PointerConstraints(const PointerConstraints &) = delete;
    PointerConstraints &operator=(const PointerConstraints &) = delete;
    ~PointerConstraints() override;

    // A surface that already has a constraint refuses the new one, which is told so and is defunct.
    bool add(PointerConstraintDelegate *delegate);

    void remove(PointerConstraintDelegate *delegate);

    PointerConstraintDelegate *active() const { return m_active; }
    base::Signal<> activeChanged;

    bool isLocked() const;

    void update();

    // `from` and `to` are in output coordinates.
    base::PointF confine(const base::PointF &from, const base::PointF &to) const;

    // Refuses any constraint until a click permits them again. Returns whether one was active.
    bool breakByUserAction();

    void onPointerPressed(compositor::Surface *surface);

    std::function<void(const base::PointF &)> warp;

    void onSurfaceDestroying(compositor::Surface *surface) override;

private:
    bool canActivate(PointerConstraintDelegate *delegate) const;

    // In surface coordinates.
    base::Region effectiveRegion(PointerConstraintDelegate *delegate) const;

    // In output coordinates.
    std::optional<base::PointF> hintPosition() const;

    void deactivate();
    void erase(PointerConstraintDelegate *delegate);

    Seat *m_seat;
    std::map<compositor::Surface *, PointerConstraintDelegate *> m_constraints;
    PointerConstraintDelegate *m_active = nullptr;
    bool m_capturePermitted = true;
};

} // namespace cutefish::input

#endif // CUTEFISH_INPUT_POINTER_CONSTRAINTS_H_
