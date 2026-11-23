#include "hwshqtb/gui/backing_store_t.h"

#include "detail/surface.h"

namespace hwshqtb::gui {
    struct backing_store_t::impl {
        /// @brief Wrap @p target without allocating anything: the first resize() creates the surface.
        explicit impl(void* target): surface(extent_t{}, target) { }

        /// @brief The bound surface: the persistent pixels, the render-target switch and the present.
        detail::sdl_bound_surface_t surface;
    };

    backing_store_t::backing_store_t(void* target):
        _impl(std::make_unique<impl>(target)) { }

    backing_store_t::~backing_store_t() = default;

    bool backing_store_t::resize(extent_t pixel_size) {
        return _impl->surface.resize(pixel_size);
    }

    bool backing_store_t::is_valid() const noexcept {
        return _impl->surface.is_valid();
    }

    extent_t backing_store_t::size() const noexcept {
        return _impl->surface.size();
    }

    bool backing_store_t::begin_paint() {
        return _impl->surface.begin_paint();
    }

    void backing_store_t::end_paint() {
        _impl->surface.end_paint();
    }

    void backing_store_t::flush(const region_t& region) {
        _impl->surface.flush(region);
    }

    bool backing_store_t::scroll(rect_t rect, int dx, int dy) {
        return _impl->surface.scroll(rect, dx, dy);
    }
}
