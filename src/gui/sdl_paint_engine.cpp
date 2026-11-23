#include "sdl_paint_engine.h"

#include <SDL3/SDL.h>

#include "hwshqtb/gui/font_engine_t.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/paint_device_t.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace hwshqtb::gui::detail {
    namespace {
        /// @brief The opaque white ink every glyph texture stores; draw_glyphs() tints it per run.
        constexpr std::uint32_t glyph_ink = 0xFFFFFFFFu;

        /// @brief How many glyph textures one paint engine keeps before dropping the whole cache.
        /// @note CLAIM: a whole-cache flush, not an LRU. Hit rate is not the bottleneck here —
        ///       determinism is: the cache is an upload optimisation, and dropping all of it is one
        ///       branch with no per-entry bookkeeping to get wrong. 1024 textures is a screenful of
        ///       text many times over, so a real UI never reaches the cap; a font with a large
        ///       character set that does (a CJK face) pays one re-upload per cached glyph afterwards.
        constexpr std::size_t max_cached_glyph_textures = 1024;

        /// @brief One cached glyph texture and the mask size it was built from.
        struct glyph_texture_t {
            /// @brief The uploaded texture, or null when this glyph has no pixels or SDL refused it.
            SDL_Texture* texture = nullptr;
            /// @brief The mask width in pixels.
            int width = 0;
            /// @brief The mask height in pixels.
            int height = 0;
            /// @brief The left side bearing of the mask (how far right of the pen its first column is).
            int bearing_x = 0;
        };
    }

    struct sdl_paint_engine_t::impl {
        /// The renderer this engine draws through; not owned (the device owns it).
        SDL_Renderer* renderer = nullptr;
        /// Whether begin() opened a pass and end() has not closed it yet.
        bool active = false;
        /// One texture per (engine, glyph), built on first use and reused afterwards.
        /// @note Keyed by the engine POINTER: engines belong to the font database and outlive this
        ///       cache, which is what makes a pointer a stable identity.
        std::map<std::pair<const font_engine_t*, std::uint32_t>, glyph_texture_t> glyphs;

        /// The cached textures wrap the renderer's texture support and must die before the renderer.
        ~impl() {
            for (auto& entry : glyphs) {
                if (entry.second.texture != nullptr) {
                    SDL_DestroyTexture(entry.second.texture);
                }
            }
        }
    };

    sdl_paint_engine_t::sdl_paint_engine_t(void* renderer):
        _impl(std::make_unique<impl>()) {
        _impl->renderer = static_cast<SDL_Renderer*>(renderer);
    }

    sdl_paint_engine_t::~sdl_paint_engine_t() = default;

    engine_feature_t sdl_paint_engine_t::features() const noexcept {
        // No antialiasing: SDL3 has no antialiasing control for its primitive rasteriser.
        return engine_feature_t{ engine_feature_flag::PRIMITIVE_TRANSFORM } | engine_feature_flag::PIXMAP_TRANSFORM
             | engine_feature_flag::CLIP | engine_feature_flag::BLEND;
    }

    bool sdl_paint_engine_t::begin(paint_device_t& device) {
        if (_impl->renderer == nullptr || device.size().isEmpty()) {
            return false;
        }
        if (!SDL_SetRenderDrawBlendMode(_impl->renderer, SDL_BLENDMODE_BLEND)) {
            return false;
        }
        if (!SDL_SetRenderClipRect(_impl->renderer, nullptr)) {
            return false;
        }
        if (!SDL_SetRenderScale(_impl->renderer, 1.0f, 1.0f)) {
            return false;
        }
        _impl->active = true;
        return true;
    }

    bool sdl_paint_engine_t::end() {
        if (!_impl->active) {
            return true;
        }
        _impl->active = false;
        if (_impl->renderer == nullptr) {
            return false;
        }
        // Flush the backend's queued commands. For the software renderer over a surface this is what
        // applies the drawing to the pixels; a window-backed device presents on top of that.
        return SDL_FlushRenderer(_impl->renderer);
    }

    void sdl_paint_engine_t::update_state(const engine_state_t& state) {
        if (!_impl->active || _impl->renderer == nullptr) {
            return;
        }
        const std::uint32_t dirty = state.dirty_flags();
        if ((dirty & engine_state_t::dirty_color) != 0) {
            const color_t color = state.color();
            (void)SDL_SetRenderDrawColor(_impl->renderer, color.red(), color.green(), color.blue(), color.alpha());
        }
        if ((dirty & engine_state_t::dirty_clip) != 0) {
            if (!state.clip_enabled()) {
                (void)SDL_SetRenderClipRect(_impl->renderer, nullptr);
            } else {
                const rect_t clip = state.clip();
                // An empty clip must draw nothing: SDL wants a non-negative extent, so an empty
                // rectangle becomes a zero-sized clip rather than an error.
                const SDL_Rect sdl_clip{ clip.x(), clip.y(), clip.width() > 0 ? clip.width() : 0,
                                         clip.height() > 0 ? clip.height() : 0 };
                (void)SDL_SetRenderClipRect(_impl->renderer, &sdl_clip);
            }
        }
        if ((dirty & engine_state_t::dirty_transform) != 0) {
            const transform_t transform = state.transform();
            // SDL's renderer coordinates support scaling only (no rotation/skew/translation), so the
            // linear diagonal is the part that can be honoured.
            (void)SDL_SetRenderScale(_impl->renderer, static_cast<float>(transform.m11()),
                                     static_cast<float>(transform.m22()));
        }
    }

    void sdl_paint_engine_t::draw_rects(const rect_t* rects, int count, bool fill) {
        if (!_impl->active || _impl->renderer == nullptr || rects == nullptr || count <= 0) {
            return;
        }
        // Empty rectangles are dropped here, not handed to SDL: SDL normalises a negative extent by
        // flipping it, while rect_t defines it as empty (draws nothing). Integer coordinates are
        // exactly representable as float, and SDL truncates them back to whole pixels.
        std::vector<SDL_FRect> batch;
        batch.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const rect_t& rect = rects[i];
            if (rect.isEmpty()) {
                continue;
            }
            batch.push_back(SDL_FRect{ static_cast<float>(rect.x()), static_cast<float>(rect.y()),
                                       static_cast<float>(rect.width()), static_cast<float>(rect.height()) });
        }
        if (batch.empty()) {
            return;
        }
        const int batch_count = static_cast<int>(batch.size());
        if (fill) {
            (void)SDL_RenderFillRects(_impl->renderer, batch.data(), batch_count);
        } else {
            (void)SDL_RenderRects(_impl->renderer, batch.data(), batch_count);
        }
    }

    void* sdl_paint_engine_t::ensure_glyph_texture(const font_engine_t* engine, std::uint32_t glyph) {
        if (_impl->renderer == nullptr || engine == nullptr) {
            return nullptr;
        }
        const auto key = std::make_pair(engine, glyph);
        const auto cached = _impl->glyphs.find(key);
        if (cached != _impl->glyphs.end()) {
            return cached->second.texture;
        }
        if (_impl->glyphs.size() >= max_cached_glyph_textures) {
            // Bounded growth without an LRU: see max_cached_glyph_textures.
            for (auto& entry : _impl->glyphs) {
                if (entry.second.texture != nullptr) {
                    SDL_DestroyTexture(entry.second.texture);
                }
            }
            _impl->glyphs.clear();
        }
        glyph_texture_t entry;
        glyph_bitmap_t bitmap;
        if (!engine->glyph_bitmap(glyph, bitmap) || bitmap.alpha == nullptr || bitmap.width <= 0
            || bitmap.height <= 0) {
            // The engine has no pixels for this glyph (a space): remember the answer so it is not asked
            // again, and draw nothing.
            _impl->glyphs.emplace(key, entry);
            return nullptr;
        }
        SDL_Surface* surface = SDL_CreateSurface(bitmap.width, bitmap.height, SDL_PIXELFORMAT_ARGB8888);
        if (surface == nullptr) {
            return nullptr; // not cached: a later run retries the upload
        }
        // The mask is 8-bit coverage; the texture is white ink with that coverage as alpha, so the run's
        // colour is applied later as a texture tint. Only ink pixels carry alpha, so SDL blends the rest.
        (void)SDL_FillSurfaceRect(surface, nullptr, 0);
        const bool must_lock = SDL_MUSTLOCK(surface);
        if (must_lock && !SDL_LockSurface(surface)) {
            SDL_DestroySurface(surface);
            return nullptr;
        }
        auto* pixels = static_cast<std::uint32_t*>(surface->pixels);
        const int stride = surface->pitch / static_cast<int>(sizeof(std::uint32_t));
        for (int row = 0; row < bitmap.height; ++row) {
            const std::uint8_t* const source = bitmap.alpha + static_cast<std::ptrdiff_t>(row) * bitmap.pitch;
            for (int column = 0; column < bitmap.width; ++column) {
                const std::uint32_t coverage = source[column];
                if (coverage != 0u) {
                    pixels[static_cast<std::ptrdiff_t>(row) * stride + column] =
                        (coverage << 24) | (glyph_ink & 0x00FFFFFFu);
                }
            }
        }
        if (must_lock) {
            SDL_UnlockSurface(surface);
        }
        SDL_Texture* const texture = SDL_CreateTextureFromSurface(_impl->renderer, surface);
        SDL_DestroySurface(surface);
        if (texture == nullptr) {
            return nullptr;
        }
        (void)SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        // Bitmap and rasterised masks are nearest sampled: a 1:1 blit copies the pixels exactly, and a
        // scaled transform keeps the glyph edges crisp instead of blurring them.
        (void)SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        entry.texture = texture;
        entry.width = bitmap.width;
        entry.height = bitmap.height;
        entry.bearing_x = bitmap.bearing_x;
        _impl->glyphs.emplace(key, entry);
        return texture;
    }

    void sdl_paint_engine_t::draw_glyphs(const glyph_run_t& run) {
        if (!_impl->active || _impl->renderer == nullptr || run.engine == nullptr) {
            return;
        }
        // Defensive: the documented contract is parallel arrays; anything else is not drawable.
        if (run.glyphs == nullptr || run.positions == nullptr || run.count == 0) {
            return;
        }
        // One ascent for the whole run: a run never mixes engines, so every glyph shares the baseline.
        const float ascent = static_cast<float>(run.engine->ascent());
        for (std::size_t i = 0; i < run.count; ++i) {
            SDL_Texture* const texture = static_cast<SDL_Texture*>(
                ensure_glyph_texture(run.engine, run.glyphs[i]));
            if (texture == nullptr) {
                continue; // no pixels for this glyph (a space)
            }
            const auto cached = _impl->glyphs.find(std::make_pair(run.engine, run.glyphs[i]));
            if (cached == _impl->glyphs.end()) {
                continue;
            }
            const glyph_texture_t& entry = cached->second;
            const point_t pen = run.positions[i];
            // The texture is white ink, so the run's colour is applied as a tint (alpha included).
            (void)SDL_SetTextureColorMod(texture, run.color.red(), run.color.green(), run.color.blue());
            (void)SDL_SetTextureAlphaMod(texture, run.color.alpha());
            // Horizontal: the mask starts bearing_x right of the pen (GDI's left side bearing; the built-in
            // face reports 0). Vertical: the engine already baked the glyph's offset into the mask, so the
            // mask's top row is the ascent line and no vertical bearing travels with it.
            const SDL_FRect target{ static_cast<float>(pen.x() + entry.bearing_x),
                                    static_cast<float>(pen.y()) - ascent,
                                    static_cast<float>(entry.width),
                                    static_cast<float>(entry.height) };
            (void)SDL_RenderTexture(_impl->renderer, texture, nullptr, &target);
        }
    }

    void sdl_paint_engine_t::draw_image(const rect_t& destination, const image_view_t& image) {
        if (!_impl->active || _impl->renderer == nullptr || image.pixels == nullptr || image.width <= 0
            || image.height <= 0 || image.pitch < image.width || destination.isEmpty()) {
            return;
        }
        // The image is BORROWED for this call only, so it is uploaded and released here: a texture cache
        // would have to know when the pixels changed, which a caller-owned buffer cannot tell us.
        SDL_Texture* const texture = SDL_CreateTexture(_impl->renderer, SDL_PIXELFORMAT_ARGB8888,
                                                       SDL_TEXTUREACCESS_STATIC, image.width, image.height);
        if (texture == nullptr) {
            return;
        }
        if (!SDL_UpdateTexture(texture, nullptr, image.pixels,
                               image.pitch * static_cast<int>(sizeof(color_t)))) {
            SDL_DestroyTexture(texture);
            return;
        }
        (void)SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        // PIXMAP_TRANSFORM: the destination may be a different size than the image, and nearest sampling
        // keeps a scaled blit's pixels exact instead of blurring them.
        (void)SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        const SDL_FRect target{ static_cast<float>(destination.x()),
                                static_cast<float>(destination.y()),
                                static_cast<float>(destination.width()),
                                static_cast<float>(destination.height()) };
        (void)SDL_RenderTexture(_impl->renderer, texture, nullptr, &target);
        SDL_DestroyTexture(texture);
    }
}