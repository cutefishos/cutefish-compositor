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
    // Drops what no draw has used for a while; called with the context current.
    void frameEnded();
    // Set when a kept backdrop went: a set or mask may now be held for no
    // blur that is still shown.
    bool mayHoldUnshown() const { return m_mayHoldUnshown; }
    // Frees those; with the context current, outside a frame. Whatever a
    // blur that keeps nothing used ages by frames only.
    void releaseUnshown();
    struct Holding {
        size_t scratch = 0;
        size_t masks = 0;
        size_t kept = 0;
    };
    Holding holding() const { return {m_scratch.size(), m_masks.size(), m_kept.size()}; }
    // Whether the blur of surface `owner` kept its result in the target `scope` names.
    bool keeps(uint64_t scope, uint64_t owner) const;
    // In a pass, what of that blur's backdrop its next draw redoes from its
    // kept result. Nothing when that draw starts from scratch.
    std::optional<base::Rect> keptChange(uint64_t scope, uint64_t owner) const;
    // Starts a pass into that target: the blurs of `fresh` draw what they kept
    // in the last one, redone where their backdrop changed; the others'
    // results are dropped.
    void beginPass(uint64_t scope, const std::vector<KeptBackdrop> &fresh);
    // `lens` is the exact device rectangle; it is rounded outward to pixels here.
    // With a `scope`, the blurred backdrop is kept for the next pass, by `owner`.
    void draw(Renderer &renderer, const base::RectF &lens, int radius, const base::Rect &clip,
              const GlassParams &glass, double scale, const ui::Path &shape,
              const std::vector<BackdropSource> &sources, std::vector<RoundedShape> &clips,
              const std::function<void(const base::RectF &, float *)> &quadFor,
              const std::function<void(const GlProgram &)> &applyRoundedClip,
              uint64_t scope = 0, uint64_t owner = 0);

private:
    // A blur that keeps its backdrop, by the pass and the surface it is for.
    struct User {
        uint64_t scope = 0;
        uint64_t owner = 0;

        bool operator==(const User &) const = default;
    };

    // Who a set or mask is held for: users whose kept backdrops are gone are
    // dropped as others come, so the list is never longer than the kept list.
    struct Users {
        std::vector<User> users;
        // Used by a blur that keeps nothing, which no kept backdrop vouches for.
        bool anonymous = false;
    };

    // Kept per size, so the dock, the panel and the windows drawn in one
    // frame do not reallocate each other's textures; plain blurs share them.
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

    // A blurred backdrop, drawn again while what lies behind it stays the same.
    struct Kept {
        uint64_t scope = 0;
        uint64_t owner = 0;
        base::Rect coverage;
        base::Size reduced;
        base::Size allocated;
        float sigma = 0;
        // Logical; what of the backdrop changed since the result was made.
        base::Rect changed;
        // A surface drawn twice in one pass keeps only its first blur.
        bool drawn = false;
        uint64_t used = 0;
        std::unique_ptr<Texture> result;
    };

    // A plain blur's set may be larger than `size`, unless `exact`.
    Scratch &scratch(const User &user, const base::Size &coverage, const base::Size &size,
                     bool exact = false);
    // Copies `coverage` of the target into `textures.source`, reduced to `reduced`.
    // A non-empty `texels`, rows top-down, is all of it that is written; the
    // rest keeps what it holds.
    bool captureBackdrop(Renderer &renderer, Scratch &textures,
                         const std::vector<BackdropSource> &sources, const base::Rect &coverage,
                         const base::Size &reduced, const base::Rect &allocatedBounds,
                         const base::Rect &texels = {});
    // Copies `coverage` of a lone upright source into the top left of the
    // bound pass at `reduced` in one blit, inside the pass's scissor. False
    // when it cannot, and nothing is drawn.
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
    std::shared_ptr<EglContext> m_context;
    GlProgram m_blurProgram;
    GlProgram m_blurMaskProgram;
    GlProgram m_glassProgram;
    uint64_t m_frame = 0;
    std::list<Scratch> m_scratch;
    std::list<Mask> m_masks;
    std::list<Kept> m_kept;
    bool m_mayHoldUnshown = false;
};

} // namespace cutefish::render
