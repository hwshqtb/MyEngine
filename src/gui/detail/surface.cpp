/// @file detail/surface.cpp
/// @brief Internal SDL pixel surface: the persistent surface behind a window's backing store.

#include "surface.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace hwshqtb::gui::detail {
    // ---- sdl_bound_surface_t ----

    sdl_bound_surface_t::sdl_bound_surface_t(extent_t size, void* target):
        _renderer(static_cast<SDL_Renderer*>(target)) {
        if (!size.isEmpty()) {
            (void)resize(size);
        }
    }

    sdl_bound_surface_t::~sdl_bound_surface_t() {
        destroy_texture();
    }

    bool sdl_bound_surface_t::resize(extent_t size) {
        if (_renderer == nullptr) {
            return false;
        }
        if (size.isEmpty()) {
            destroy_texture();
            return false;
        }
        if (_texture != nullptr && _size == size) {
            return true; // already the requested size: keep the pixels
        }
        destroy_texture();
        if (_refused) {
            return false; // the driver cannot render into a surface, so do not try again
        }
        SDL_Texture* const texture = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_TARGET, size.width(), size.height());
        if (texture == nullptr) {
            return false;
        }
        // Blending is off and sampling is 1:1: flush() copies the surface verbatim.
        (void)SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
        (void)SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        _texture = texture;
        _size = size;
        return true;
    }

    extent_t sdl_bound_surface_t::size() const noexcept {
        return _texture != nullptr ? _size : extent_t{};
    }

    bool sdl_bound_surface_t::is_valid() const noexcept {
        return _texture != nullptr && !_refused;
    }

    bool sdl_bound_surface_t::begin_paint() {
        if (!is_valid()) {
            return false;
        }
        if (!SDL_SetRenderTarget(_renderer, _texture)) {
            // The driver cannot draw into this surface. Drop it so nothing half-attached survives,
            // and remember the refusal: the caller falls back to painting the target directly.
            _refused = true;
            destroy_texture();
            return false;
        }
        _painting = true;
        return true;
    }

    void sdl_bound_surface_t::end_paint() {
        if (!_painting) {
            return;
        }
        _painting = false;
        (void)SDL_SetRenderTarget(_renderer, nullptr);
    }

    void sdl_bound_surface_t::flush(const region_t& region) {
        if (!is_valid()) {
            return;
        }
        // Flush restores the window target itself rather than relying on the caller's end_paint(): the
        // pixel copy is meaningless while the surface is still the target.
        if (_painting) {
            end_paint();
        }
        (void)SDL_SetRenderDrawBlendMode(_renderer, SDL_BLENDMODE_NONE);
        (void)SDL_SetRenderClipRect(_renderer, nullptr);
        (void)SDL_SetRenderScale(_renderer, 1.0f, 1.0f);
        for (const rect_t& area : region.rects()) {
            if (area.isEmpty()) {
                continue;
            }
            // Source and destination are the same rectangle: the surface is a pixel copy of the
            // window's client area, not a scaled image.
            const SDL_FRect pixel_rect{ static_cast<float>(area.x()), static_cast<float>(area.y()),
                                        static_cast<float>(area.width()), static_cast<float>(area.height()) };
            (void)SDL_RenderTexture(_renderer, _texture, &pixel_rect, &pixel_rect);
        }
        (void)SDL_RenderPresent(_renderer);
    }

    bool sdl_bound_surface_t::scroll(rect_t rect, int dx, int dy) {
        if (!is_valid() || _painting) {
            return false; // no surface, or the surface is the current target and cannot be a source
        }
        if (rect.isEmpty()) {
            return true; // nothing to move, nothing exposed
        }
        if (dx == 0 && dy == 0) {
            return true; // a zero shift exposes nothing
        }
        // A shift of at least the extent leaves no old pixel inside the rectangle: the caller has to
        // repaint the whole area, and there is nothing worth copying.
        if (dx <= -rect.width() || dx >= rect.width() || dy <= -rect.height() || dy >= rect.height()) {
            return false;
        }
        if (!ensure_scratch(extent_t{ rect.width(), rect.height() })) {
            return false;
        }

        // Both passes copy pixels verbatim: no blending, no scaling, nearest sampling.
        (void)SDL_SetRenderDrawBlendMode(_renderer, SDL_BLENDMODE_NONE);
        (void)SDL_SetRenderScale(_renderer, 1.0f, 1.0f);
        const SDL_FRect area{ static_cast<float>(rect.x()), static_cast<float>(rect.y()),
                              static_cast<float>(rect.width()), static_cast<float>(rect.height()) };
        const SDL_FRect origin{ 0.0f, 0.0f, static_cast<float>(rect.width()),
                                static_cast<float>(rect.height()) };
        const SDL_FRect moved{ static_cast<float>(rect.x() + dx), static_cast<float>(rect.y() + dy),
                               static_cast<float>(rect.width()), static_cast<float>(rect.height()) };

        // (1) Surface -> scratch. Only a scratch write, so a refusal here leaves the surface intact.
        (void)SDL_SetRenderClipRect(_renderer, nullptr);
        if (!SDL_SetRenderTarget(_renderer, _scratch)) {
            (void)SDL_SetRenderTarget(_renderer, nullptr);
            return false;
        }
        const bool copied = SDL_RenderTexture(_renderer, _texture, &area, &origin) == true;
        (void)SDL_SetRenderTarget(_renderer, nullptr);
        if (!copied) {
            return false;
        }

        // (2) Scratch -> surface, clipped to the scrolled rectangle: the pixels that the shift pushes
        // out of `rect` must not land outside it. SDL clips a draw to the target's clip rectangle, so
        // the destination rectangle may be given in full.
        if (!SDL_SetRenderTarget(_renderer, _texture)) {
            (void)SDL_SetRenderTarget(_renderer, nullptr);
            return false;
        }
        const SDL_Rect clip{ rect.x(), rect.y(), rect.width(), rect.height() };
        (void)SDL_SetRenderClipRect(_renderer, &clip);
        const bool moved_ok = SDL_RenderTexture(_renderer, _scratch, &origin, &moved) == true;
        (void)SDL_SetRenderClipRect(_renderer, nullptr);
        (void)SDL_SetRenderTarget(_renderer, nullptr);
        return moved_ok;
    }

    void sdl_bound_surface_t::destroy_texture() noexcept {
        if (_texture != nullptr) {
            SDL_DestroyTexture(_texture);
            _texture = nullptr;
        }
        if (_scratch != nullptr) {
            SDL_DestroyTexture(_scratch);
            _scratch = nullptr;
        }
        _scratch_size = extent_t{};
        _size = extent_t{};
    }

    bool sdl_bound_surface_t::ensure_scratch(extent_t size) {
        if (_scratch != nullptr && _scratch_size == size) {
            return true;
        }
        if (_scratch != nullptr) {
            SDL_DestroyTexture(_scratch);
            _scratch = nullptr;
            _scratch_size = extent_t{};
        }
        SDL_Texture* const created = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_TARGET, size.width(), size.height());
        if (created == nullptr) {
            return false;
        }
        (void)SDL_SetTextureBlendMode(created, SDL_BLENDMODE_NONE);
        (void)SDL_SetTextureScaleMode(created, SDL_SCALEMODE_NEAREST);
        _scratch = created;
        _scratch_size = size;
        return true;
    }
}
