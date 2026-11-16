// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "render/renderer.h"

#include <algorithm>

namespace cutefish::render {

namespace {
std::optional<Color> s_opaqueBackdrop;
}

std::optional<Color> opaqueBackdrop()
{
    return s_opaqueBackdrop;
}

void setOpaqueBackdrop(std::optional<Color> color)
{
    s_opaqueBackdrop = color;
}

const Renderer::TextureSet& Renderer::cachedTextureSet(const std::string& key,
                                                       const std::function<TextureSet()>& make)
{
    const auto found =
        std::find_if(m_textureCache.begin(), m_textureCache.end(), [&](const auto& item) {
            return item.key == key;
        });
    if (found != m_textureCache.end()) {
        m_textureCache.splice(m_textureCache.begin(), m_textureCache, found);
        return m_textureCache.front().textures;
    }

    // Factory only rasterizes/uploads; it must not call back into the scene.
    auto textures = make();
    ++m_textureCacheMisses;
    if (m_textureCache.size() == textureCacheLimit)
        m_textureCache.pop_back();
    m_textureCache.push_front({key, std::move(textures)});
    return m_textureCache.front().textures;
}

} // namespace cutefish::render
