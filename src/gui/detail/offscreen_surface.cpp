/// @file detail/offscreen_surface.cpp
/// @brief The ARGB8888 pixel buffer + software renderer shared by image_t and offscreen_window_t.

#include "offscreen_surface.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "../sdl_paint_engine.h"

namespace hwshqtb::gui::detail {
    offscreen_surface::offscreen_surface(extent_t size) {
        if (size.width() <= 0 || size.height() <= 0) {
            throw std::invalid_argument("image_t: the extent must be positive");
        }
        _surface = SDL_CreateSurface(size.width(), size.height(), SDL_PIXELFORMAT_ARGB8888);
        if (_surface == nullptr) {
            throw std::runtime_error(std::string("image_t: SDL_CreateSurface failed: ") + SDL_GetError());
        }
        _renderer = SDL_CreateSoftwareRenderer(_surface);
        if (_renderer == nullptr) {
            SDL_DestroySurface(_surface);
            _surface = nullptr;
            throw std::runtime_error(std::string("image_t: SDL_CreateSoftwareRenderer failed: ")
                                     + SDL_GetError());
        }
        _size = size;
        // The engine exists for as long as the pixels do: created here, in the setup, so that
        // paint_engine() is a pure query for the rest of the surface's life.
        _engine = std::make_unique<sdl_paint_engine_t>(_renderer);
    }

    offscreen_surface::~offscreen_surface() {
        _engine.reset(); // it wraps the renderer
        if (_renderer != nullptr) {
            SDL_DestroyRenderer(_renderer);
            _renderer = nullptr;
        }
        if (_surface != nullptr) {
            SDL_DestroySurface(_surface);
            _surface = nullptr;
        }
    }

    extent_t offscreen_surface::size() const noexcept {
        return _surface != nullptr ? _size : extent_t{};
    }

    paint_engine_t* offscreen_surface::paint_engine() noexcept {
        return _engine.get();
    }

    void* offscreen_surface::target() const noexcept {
        return _renderer;
    }

    color_t* offscreen_surface::pixels(int& pitch) {
    pitch = 0;
    if (_surface == nullptr || _surface->pixels == nullptr) {
        return nullptr;
    }
    flush_engine();
    pitch = _surface->pitch / static_cast<int>(sizeof(color_t));
    return static_cast<color_t*>(_surface->pixels);
}
const color_t* offscreen_surface::pixels(int& pitch) const {
    pitch = 0;
    if (_surface == nullptr || _surface->pixels == nullptr) {
        return nullptr;
    }
    // Queued drawing must reach the surface before anything reads it.
    flush_engine();
    pitch = _surface->pitch / static_cast<int>(sizeof(color_t));
    return static_cast<const color_t*>(_surface->pixels);
}
color_t offscreen_surface::pixel(int x, int y) const {
        if (_surface == nullptr || x < 0 || y < 0 || x >= _size.width() || y >= _size.height()) {
            return color_t::transparent_color();
        }
        // Queued drawing must reach the surface before it can be inspected.
        flush_engine();

        SDL_Surface* surface = _surface;
        const bool must_lock = SDL_MUSTLOCK(surface);
        if (must_lock && !SDL_LockSurface(surface)) {
            return color_t::transparent_color();
        }
        const auto* pixels = static_cast<const std::uint32_t*>(surface->pixels);
        const std::uint32_t argb =
            pixels[static_cast<std::ptrdiff_t>(y) * (surface->pitch / 4) + static_cast<std::ptrdiff_t>(x)];
        if (must_lock) {
            SDL_UnlockSurface(surface);
        }
        return color_t::from_argb(argb);
    }

    void offscreen_surface::flush_engine() const {
        if (_renderer != nullptr) {
            (void)SDL_FlushRenderer(_renderer);
        }
    }
}
