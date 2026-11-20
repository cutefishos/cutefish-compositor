// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_RENDER_GLASS_H_
#define CUTEFISH_RENDER_GLASS_H_

#include <algorithm>

namespace cutefish::render {

// Lengths are logical pixels and angles degrees; the defaults are the shell
// panel material.
struct GlassParams {
    bool enabled = false;
    // Backdrop softening before refraction. Far smaller than the plain blur
    // radius, which would leave nothing for the lens to bend.
    float blurSigma = 1.2f;
    // Clamped to half the smaller side of the rectangle; zero falls back to its
    // corner radius.
    float bevel = 14.0f;
    float depth = 16.0f;
    float ior = 1.7f;
    // Superellipse exponent of the surface profile.
    float profile = 2.5f;
    // Past about 4x local compression the lip smears into a second outline;
    // widen the bevel instead.
    float displacement = 36.0f;
    // Red/blue split as a fraction of the displacement.
    float chroma = 0.05f;
    float lightAngle = 50.0f;
    float specularIntensity = 0.0f;
    float shininess = 42.0f;
    float sheen = 0.0f;
    float ambientOcclusion = 0.0f;
    float ambientOcclusionRadius = 4.0f;
    // 1 leaves the backdrop as it is.
    float saturation = 1.0f;
    // When set it replaces the window outline.
    float rim = 0.0f;
    float tintRed = 1.0f;
    float tintGreen = 1.0f;
    float tintBlue = 1.0f;
    float tintAlpha = 0.12f;
    float opacity = 1.0f;
    float plainSigma = 30.0f;
    // Premultiplied; `fillMode` blends a straight colour over it.
    float fillTint[4] = {0, 0, 0, 0};
    int fillMode = 0;
    float fillColor[4] = {0, 0, 0, 0};
    // The shell's own; the wire protocol never sets it.
    bool ignoresOpaqueBackdrop = false;
};

// As the W3C compositing modes do.
enum class BackdropBlend {
    None,
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity,
};

inline bool operator==(const GlassParams &a, const GlassParams &b)
{
    return a.enabled == b.enabled && a.blurSigma == b.blurSigma && a.bevel == b.bevel
            && a.depth == b.depth && a.ior == b.ior && a.profile == b.profile
            && a.displacement == b.displacement && a.chroma == b.chroma
            && a.lightAngle == b.lightAngle && a.specularIntensity == b.specularIntensity
            && a.shininess == b.shininess && a.sheen == b.sheen
            && a.ambientOcclusion == b.ambientOcclusion
            && a.ambientOcclusionRadius == b.ambientOcclusionRadius
            && a.saturation == b.saturation && a.rim == b.rim && a.tintRed == b.tintRed
            && a.tintGreen == b.tintGreen && a.tintBlue == b.tintBlue && a.tintAlpha == b.tintAlpha
            && a.opacity == b.opacity && a.plainSigma == b.plainSigma && a.fillMode == b.fillMode
            && a.ignoresOpaqueBackdrop == b.ignoresOpaqueBackdrop
            && std::equal(a.fillTint, a.fillTint + 4, b.fillTint)
            && std::equal(a.fillColor, a.fillColor + 4, b.fillColor);
}

inline bool operator!=(const GlassParams &a, const GlassParams &b)
{
    return !(a == b);
}

} // namespace cutefish::render

#endif // CUTEFISH_RENDER_GLASS_H_
