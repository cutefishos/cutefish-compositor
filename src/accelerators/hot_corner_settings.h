// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <functional>
#include <optional>

#include <memory>
#include <string>

#include "accelerators/accelerator_actions.h"
#include "base/settings.h"
#include "base/signal.h"

namespace cutefish::input {
class DBusExport;
}

namespace cutefish::accelerators {

enum class HotCorner {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

// No modifiers means any may be held.
struct HotCornerBinding {
    std::optional<AcceleratorAction> action;
    uint32_t modifiers = 0;

    bool operator==(const HotCornerBinding &) const = default;
};

// Kept in compositor.conf [HotCorners]; an empty action turns the corner off.
class HotCornerSettings
{
public:
    explicit HotCornerSettings(const std::string &settingsFile = {});
    ~HotCornerSettings();

    bool registerService();

    const HotCornerBinding &binding(HotCorner corner) const;

    void setModifierSource(std::function<uint32_t()> source) { m_modifierSource = std::move(source); }

    std::string topLeft() const { return actionName(HotCorner::TopLeft); }
    int topLeftModifiers() const { return modifiers(HotCorner::TopLeft); }
    std::string topRight() const { return actionName(HotCorner::TopRight); }
    int topRightModifiers() const { return modifiers(HotCorner::TopRight); }
    std::string bottomLeft() const { return actionName(HotCorner::BottomLeft); }
    int bottomLeftModifiers() const { return modifiers(HotCorner::BottomLeft); }
    std::string bottomRight() const { return actionName(HotCorner::BottomRight); }
    int bottomRightModifiers() const { return modifiers(HotCorner::BottomRight); }

    // `corner` is top-left, top-right, bottom-left or bottom-right. An action
    // that is not known turns the corner off.
    void setCorner(const std::string &corner, const std::string &action, int modifiers);

    // A client's own modifiers can differ: the Command convention swaps keys before
    // applications see them.
    int heldModifiers() const;

    base::Signal<> changed;

private:
    std::string actionName(HotCorner corner) const;
    int modifiers(HotCorner corner) const;

    base::Settings m_settings;
    std::array<HotCornerBinding, 4> m_bindings;
    std::function<uint32_t()> m_modifierSource;
    std::unique_ptr<input::DBusExport> m_object;
};

} // namespace cutefish::accelerators
