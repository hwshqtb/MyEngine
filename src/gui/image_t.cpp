#include "hwshqtb/gui/image_t.h"

#include "detail/bmp.h"
#include "detail/offscreen_surface.h"

#include <stdexcept>
#include <utility>

namespace hwshqtb::gui {
    struct image_t::impl {
        /// @brief Allocate @p size pixels (the surface validates the extent and throws on failure).
        explicit impl(extent_t size): surface(size) { }

        /// @brief The offscreen surface: pixels, software renderer and engine.
        detail::offscreen_surface surface;
    };

    image_t::image_t(int width, int height) {
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("image_t: the extent must be positive");
        }
        _impl = std::make_unique<impl>(extent_t{ width, height });
    }

    image_t::image_t(extent_t size):
        image_t(size.width(), size.height()) { }

    image_t::image_t(image_t&& other) noexcept:
        _impl(std::move(other._impl)) { }

    image_t& image_t::operator=(image_t&& other) noexcept {
        if (this != &other) {
            _impl = std::move(other._impl);
        }
        return *this;
    }

    image_t::~image_t() = default;

    extent_t image_t::size() const noexcept {
        if (_impl == nullptr) {
            return extent_t{};
        }
        return _impl->surface.size();
    }

    paint_engine_t* image_t::paint_engine() noexcept {
        // A moved-from image holds no surface, and a surface without pixels holds no engine: both are
        // "this image cannot be painted right now", reported as nullptr rather than an exception.
        return _impl == nullptr ? nullptr : _impl->surface.paint_engine();
    }

    image_view_t image_t::view() const {
    image_view_t view;
    if (_impl == nullptr) {
        return view; // a moved-from image holds no surface: a null view, not a crash
    }
    int pitch = 0;
    view.pixels = _impl->surface.pixels(pitch);
    const ::hwshqtb::gui::extent_t extent = _impl->surface.size();
    view.width = extent.width();
    view.height = extent.height();
    view.pitch = pitch;
    return view;
}
color_t* image_t::writable_pixels(int& pitch) {
    return _impl != nullptr ? _impl->surface.pixels(pitch) : nullptr;
}

color_t image_t::pixel(int x, int y) const {
        if (_impl == nullptr) {
            return color_t::transparent_color();
        }
        return _impl->surface.pixel(x, y);
    }
}
