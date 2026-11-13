// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include "render/egl_renderer.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "base/geometry.h"

namespace cutefish::render {

struct TargetRetirement {
    EglRenderer *renderer;
    EglRenderTarget *target;

    ~TargetRetirement() { renderer->releaseTarget(target); }
};

// Only replaces display/target acquisition. Shader, upload, cache, draw and
// blending are the real EglRenderer paths, without DRM or a second compositor.
class EglRendererTestAccess
{
public:
    static void applyRepaint(EglRenderer &renderer, const base::Rect &damage)
    {
        renderer.applyRepaint(damage);
    }

    static std::unique_ptr<EglRenderer> create(int alphaSize = 8)
    {
        auto context = EglContext::createSurfaceless(alphaSize);
        if (!context)
            return nullptr;
        auto renderer = std::unique_ptr<EglRenderer>(new EglRenderer(std::move(context)));
        if (!renderer->initPrograms())
            return nullptr;
        return renderer;
    }

    static bool initialize(EglRenderer &renderer, const char *fragmentOverride = nullptr,
                           const char *externalOverride = nullptr,
                           const char *backdropOverride = nullptr)
    {
        return renderer.initPrograms(fragmentOverride, externalOverride, backdropOverride);
    }

    static std::unique_ptr<EglRenderer> uninitialized()
    {
        auto context = EglContext::createSurfaceless();
        return context ? std::unique_ptr<EglRenderer>(new EglRenderer(std::move(context)))
                       : nullptr;
    }

    static bool hasPrograms(const EglRenderer &renderer)
    {
        return renderer.m_colorProgram && renderer.m_textureProgram && renderer.m_backdrop;
    }

    static bool hasExternalProgram(const EglRenderer &renderer)
    {
        return renderer.m_externalProgram;
    }

    static bool hasBackdrop(const EglRenderer &renderer) { return bool(renderer.m_backdrop); }

    static BackdropFilter::Holding backdropHolding(const EglRenderer &renderer)
    {
        return renderer.m_backdrop ? renderer.m_backdrop->holding() : BackdropFilter::Holding();
    }

    static void setBackdropReleaseDelay(EglRenderer &renderer, std::chrono::milliseconds delay)
    {
        renderer.m_backdropReleaseDelay = delay;
    }

    static bool backdropReleaseArmed(const EglRenderer &renderer)
    {
        return renderer.m_backdropRelease && renderer.m_backdropRelease->isActive();
    }
};

} // namespace cutefish::render
