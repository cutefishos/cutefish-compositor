// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <optional>
#include <vector>

#include "render/gl_program.h"
#include "render/gl_render_pass.h"
#include "render/renderer.h"
#include "render/shape_mask.h"
#include "base/geometry.h"

namespace cutefish::render {

class BackdropFilter
{
public:
    explicit BackdropFilter(std::shared_ptr<EglContext> context)
        : m_context(std::move(context))
    {
    }

    bool initialize(const char *fragmentOverride = nullptr);
    void resetTextures();
    // Called with the context current.
    void frameEnded();
    bool mayHoldUnshown() const { return m_mayHoldUnshown; }
    // With the context current, outside a frame.
    void releaseUnshown();
    struct Holding {
        size_t scratch = 0;
        size_t masks = 0;
        size_t kept = 0;
    };
    Holding holding() const { return {m_scratch.size(), m_masks.size(), m_kept.size()}; }
    bool keeps(uint64_t scope, uint64_t owner) const;
    // Nothing when that draw starts from scratch.
    std::optional<base::Rect> keptChange(uint64_t scope, uint64_t owner) const;
    // The blurs of `fresh` redraw what they kept, redone where their backdrop
    // changed; the others' results are dropped.
    void beginPass(uint64_t scope, const std::vector<KeptBackdrop> &fresh);
    // `lens` is the exact device rectangle; with a `scope`, the result is kept
    // for the next pass, by `owner`.
    void draw(Renderer &renderer, const base::RectF &lens, int radius, const base::Rect &clip,
              const GlassParams &glass, double scale, const ui::Path &shape,
              const std::vector<BackdropSource> &sources, std::vector<RoundedShape> &clips,
              const std::function<void(const base::RectF &, float *)> &quadFor,
              const std::function<void(const GlProgram &)> &applyRoundedClip,
              uint64_t scope = 0, uint64_t owner = 0);

private:
    struct User {
        uint64_t scope = 0;
        uint64_t owner = 0;

        bool operator==(const User &) const = default;
    };

    // Users whose kept backdrops are gone are dropped as others come.
    struct Users {
        std::vector<User> users;
        bool anonymous = false;
    };

    // Kept per size, so blurs drawn in one frame do not reallocate each other's
    // textures.
    struct Scratch {
        // Empty for a plain blur, whose textures serve any coverage.
        base::Size coverage;
        base::Size size;
        uint64_t used = 0;
        std::unique_ptr<Texture> capture;
        std::unique_ptr<Texture> opaqueCapture;
        std::unique_ptr<Texture> source;
        std::unique_ptr<Texture> temp;
        std::unique_ptr<Texture> result;
        Users users;
    };
    // `shape` is relative to its own bounds, so a moving popup keeps its mask.
    struct Mask {
        ui::Path shape;
        ShapeRaster raster;
        uint64_t used = 0;
        std::unique_ptr<Texture> texture;
        Users users;
    };

    struct Kept {
        uint64_t scope = 0;
        uint64_t owner = 0;
        base::Rect coverage;
        base::Size reduced;
        base::Size allocated;
        float sigma = 0;
        // Logical.
        base::Rect changed;
        // A surface drawn twice in one pass keeps only its first blur.
        bool drawn = false;
        uint64_t used = 0;
        std::unique_ptr<Texture> result;
    };

    // A plain blur's set may be larger than `size`, unless `exact`.
    Scratch &scratch(const User &user, const base::Size &coverage, const base::Size &size,
                     bool exact = false);
    // A non-empty `texels`, rows top-down, is all that is written; the rest
    // keeps what it holds.
    bool captureBackdrop(Renderer &renderer, Scratch &textures,
                         const std::vector<BackdropSource> &sources, const base::Rect &coverage,
                         const base::Size &reduced, const base::Rect &allocatedBounds,
                         const base::Rect &texels = {});
    // False when it cannot, and nothing is drawn.
    bool blitReduced(const std::vector<BackdropSource> &sources, const base::Rect &coverage,
                     const base::Size &reduced, unsigned int destination);
    Texture *mask(Renderer &renderer, const User &user, const ui::Path &shape, const ShapeRaster &raster);
    bool hasKept(const User &user) const;
    void addUser(Users &users, const User &user) const;
    bool isUnshown(const Users &users) const;
    void applyGlassMaterial(const GlProgram &program, const GlassParams &glass,
                            const base::Size &coverage, double scale);

    static constexpr size_t kCachedSizes = 8;
    static constexpr int kScratchStep = 64;
    static constexpr uint64_t kUnusedFrames = 60;
    static constexpr int kOpaqueSide = 8;
    std::shared_ptr<EglContext> m_context;
    GlProgram m_blurProgram;
    GlProgram m_blurMaskProgram;
    GlProgram m_glassProgram;
    uint64_t m_frame = 0;
    std::list<Scratch> m_scratch;
    std::list<Mask> m_masks;
    std::list<Kept> m_kept;
    bool m_mayHoldUnshown = false;
    std::unique_ptr<Texture> m_opaque;
    Color m_opaqueColor;
};

} // namespace cutefish::render
