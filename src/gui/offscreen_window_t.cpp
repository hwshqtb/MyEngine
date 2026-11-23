/// @file offscreen_window_t.cpp
/// @brief offscreen_window_t: a window_t painted into memory, with no native window behind it.

#include "hwshqtb/gui/offscreen_window_t.h"

#include <memory>
#include <stdexcept>
#include <utility>

#include "detail/offscreen_platform_window.h"

namespace hwshqtb::gui::detail {
    /// @brief The construction hand-off: the platform itself, plus the pointer the pixel query keeps.
    struct offscreen_window_seed {
        /// @brief The platform half, handed to window_t (which owns it from then on).
        std::unique_ptr<offscreen_platform_window> owned;
        /// @brief The same object, borrowed for offscreen_window_t::pixel().
        offscreen_platform_window* borrowed = nullptr;
    };
}

namespace hwshqtb::gui {
    namespace {
        /// @brief Build the platform half and remember both handles to it.
        /// @param size The client area in pixels.
        /// @return The seed the delegating constructor passes on.
        /// @throws std::invalid_argument when @p size is empty, under THIS class's name: the shared
        ///         offscreen surface reports the same failure for image_t, and a public window must not
        ///         blame another type.
        detail::offscreen_window_seed make_seed(extent_t size) {
            if (size.isEmpty()) {
                throw std::invalid_argument("offscreen_window_t: the extent must be positive");
            }
            auto owned = std::make_unique<detail::offscreen_platform_window>(size);
            detail::offscreen_window_seed seed;
            seed.borrowed = owned.get();
            seed.owned = std::move(owned);
            return seed;
        }
    }

    offscreen_window_t::offscreen_window_t(extent_t size):
        offscreen_window_t(make_seed(size)) { }

    offscreen_window_t::offscreen_window_t(detail::offscreen_window_seed seed):
        // The base takes ownership first; seed.borrowed is a separate copy of the pointer, so it stays
        // valid while the platform object (now owned by window_t) is alive.
        window_t(std::move(seed.owned)),
        _platform(seed.borrowed) { }

    offscreen_window_t::~offscreen_window_t() = default;

    color_t offscreen_window_t::pixel(int x, int y) const noexcept {
        // _platform is non-null for every fully constructed object (the public constructor always
        // seeds it) and the platform outlives this class's members, because window_t destroys it.
        if (_platform == nullptr) {
            return color_t::transparent_color();
        }
        return _platform->pixel(x, y);
    }
}
