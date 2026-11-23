#ifndef HWSHQTB__GUI__SRC__SDL_PAINT_ENGINE_H
#define HWSHQTB__GUI__SRC__SDL_PAINT_ENGINE_H

/// @file sdl_paint_engine.h
/// @brief Internal SDL_Renderer-backed paint engine (H2). Not installed, not exported.

#include <cstdint>
#include <memory>

#include "hwshqtb/gui/paint_engine_t.h"

namespace hwshqtb::gui::detail {
    /// @brief The paint_engine_t implementation that draws through an SDL_Renderer.
    ///
    /// @par Why this class is internal, not public
    /// It can only be constructed from an SDL_Renderer*, an SDL type that must not appear in a
    /// public header; a public constructor could only take a void*, which would give up the type
    /// safety of every caller for nothing. The public contract is paint_engine_t — tests and users
    /// reach features() and drawing through paint_device_t::paint_engine(), so the concrete backend
    /// stays replaceable. A future public backend (for example drawing straight into a window
    /// renderer, G7) can be exposed once its construction no longer needs an SDL handle.
    ///
    /// @par SDL stays in the .cpp
    /// The class holds no SDL member: `impl` is defined in sdl_paint_engine.cpp, so the SDL header
    /// and the SDL types are confined to that translation unit (the renderer arrives type-erased and
    /// is cast back there).
    ///
    /// @par What it supports
    /// features() = primitive_transform | pixmap_transform | clip | blend. antialiasing is
    /// deliberately NOT claimed: SDL3 exposes no antialiasing switch for its rect/line rasteriser
    /// (antialiasing is deferred to a later stage), and a feature query must not promise something
    /// the backend cannot do.
    ///
    /// update_state() applies the colour (SDL_SetRenderDrawColor), the clip (SDL_SetRenderClipRect,
    /// disabled for the default state) and the transform. The transform is applied with
    /// SDL_SetRenderScale: SDL's renderer coordinates support a scale but no rotation, skew or
    /// translation, so only m11/m22 can be honoured. That is the whole transform the painter exposes
    /// today (identity); a general affine needs the G1/G7 work and a richer backend.
    ///
    /// @par Glyph rasterisation
    /// draw_glyphs() draws GLYPH INDICES of the run's font_engine_t: the first time a glyph is asked
    /// for, the engine rasterises it with font_engine_t::glyph_bitmap() (8-bit coverage), the backend
    /// expands that into an ARGB texture (white ink, the coverage as alpha) and caches it per
    /// (engine, glyph). A later run reuses the texture; the tint is applied per run through the texture
    /// colour/alpha modulation, exactly as the previous single-atlas implementation did.
    /// @note The cache belongs to the paint engine, so an engine that is destroyed (a font database
    ///       teardown) must not outlive its paint engine. In this build the font engine outlives every
    ///       paint engine, which is the same assumption the font database makes everywhere else.
    class sdl_paint_engine_t final: public paint_engine_t {
    public:
        /// @brief Wrap an existing SDL_Renderer (not owned).
        /// @param renderer The SDL_Renderer*, type-erased to void* so no SDL type appears in any
        ///        header. It must stay alive for the whole lifetime of the engine.
        explicit sdl_paint_engine_t(void* renderer);

        ~sdl_paint_engine_t() override;

        sdl_paint_engine_t(const sdl_paint_engine_t&) = delete;
        sdl_paint_engine_t& operator=(const sdl_paint_engine_t&) = delete;

        engine_feature_t features() const noexcept override;
        bool begin(paint_device_t& device) override;
        bool end() override;
        void update_state(const engine_state_t& state) override;
        void draw_rects(const rect_t* rects, int count, bool fill) override;
        void draw_glyphs(const glyph_run_t& run) override;
        void draw_image(const rect_t& destination, const image_view_t& image) override;

    private:
        /// @brief The texture of one (engine, glyph) pair, built on first use.
        /// @param engine The engine that rasterises the glyph; never null.
        /// @param glyph The glyph index inside that engine.
        /// @return The texture, or nullptr when the engine has no pixels for it (a space) or SDL
        ///         refused the upload.
        void* ensure_glyph_texture(const font_engine_t* engine, std::uint32_t glyph);

        struct impl;
        std::unique_ptr<impl> _impl;
    };
}

#endif
