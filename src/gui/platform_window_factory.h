#ifndef HWSHQTB__GUI__PLATFORM_WINDOW_FACTORY_H
#define HWSHQTB__GUI__PLATFORM_WINDOW_FACTORY_H

/// @file platform_window_factory.h
/// @brief Internal: the one place that knows which platform_window_t implementation this build has.

#include <memory>
#include <string_view>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/platform_window_t.h"

namespace hwshqtb::gui {
    class window_t;
}

namespace hwshqtb::gui::detail {
    /// @brief Create this build's platform window and attach it to @p owner.
    /// @param owner The window every platform report goes to; it must outlive the returned object.
    /// @param title The window title.
    /// @param size The client area in window coordinates; both sides must be positive.
    /// @return The platform half, already attached (its native event filter is installed, the platform
    ///         text input follows the current focus) and owned by the caller.
    /// @throws std::invalid_argument when @p size is empty.
    /// @throws std::runtime_error when the platform's video side is not up, or the native window or its
    ///         renderer cannot be created.
    /// @note This is the ONLY place a concrete platform implementation is chosen: window_t never names
    ///       one, so a second platform is one file and one line here.
    std::unique_ptr<platform_window_t> create_platform_window(
        window_t& owner, std::string_view title, extent_t size);
}

#endif
