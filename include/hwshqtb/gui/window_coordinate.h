#ifndef HWSHQTB__GUI__WINDOW_COORDINATE_H
#define HWSHQTB__GUI__WINDOW_COORDINATE_H

/// @file window_coordinate.h
/// @brief The pointer-coordinate conversion from WINDOW coordinates to the render/pixel space.
///
/// @par Header-only by design
/// The conversion is pure integer arithmetic whose rounding rule is part of its contract. Defining
/// it out of line in a .cpp would make the call non-constexpr, so the rule could no longer be pinned
/// by a static_assert; this header is the second sanctioned exception to the project rule
/// "declarations in headers, bodies in .cpp" (the first is geometry.h).

#include <cstdint>
#include <limits>

#include "hwshqtb/gui/geometry.h"

namespace hwshqtb::gui {
    namespace detail {
        /// @brief One axis of window_to_pixel_point(): floor(@p value * @p to / @p from).
        /// @param value The coordinate in the source units.
        /// @param from The source extent's side; must be positive.
        /// @param to The target extent's side; must be positive.
        /// @return The coordinate in the target units, rounded towards negative infinity and
        ///         saturated at the int range.
        /// @note The product uses a 64-bit intermediate: two int values near 2^31 give roughly 4.6e18,
        ///       which still fits in an int64_t, so no intermediate can overflow.
        constexpr int scale_coordinate(int value, int from, int to) noexcept {
            const std::int64_t numerator = static_cast<std::int64_t>(value) * static_cast<std::int64_t>(to);
            const std::int64_t denominator = static_cast<std::int64_t>(from);
            std::int64_t quotient = numerator / denominator;
            // C++ integer division truncates towards zero. Flooring a negative quotient has to go one
            // lower: -3 / 2 is -1 by truncation, but the cell containing -1.5 starts at -2.
            if (numerator % denominator != 0 && numerator < 0) {
                --quotient;
            }
            if (quotient > std::numeric_limits<int>::max()) {
                return std::numeric_limits<int>::max();
            }
            if (quotient < std::numeric_limits<int>::min()) {
                return std::numeric_limits<int>::min();
            }
            return static_cast<int>(quotient);
        }
    }

    /// @brief Map a point from WINDOW coordinates (logical points, what SDL reports for the pointer)
    ///        to the render/pixel space every painted rectangle lives in.
    /// @param window_point The point, in the units of @p window_size.
    /// @param window_size The client area in WINDOW coordinates (SDL_GetWindowSize).
    /// @param pixel_size The client area in PIXELS (SDL_GetWindowSizeInPixels, or the renderer's
    ///        output size when the window cannot report it).
    /// @return The point in the units of @p pixel_size: each axis is floor(window * pixel / window).
    /// @note Identity when the two extents agree, i.e. when the pixel density is 1. That is the common
    ///       case, and the only case on Windows, where the client rectangle IS the pixel grid (one
    ///       DPI-aware coordinate space), so SDL_GetWindowSize already reports physical pixels there.
    ///       A HiDPI backend reports pixel_size above window_size and the same formula maps the
    ///       pointer onto the tree's grid.
    /// @note Rounds DOWN, not to the nearest pixel: the result is the pixel cell that contains the
    ///       logical point, so a hit test lands in the cell the user pointed at. Rounding up would
    ///       move every non-integral coordinate into the cell to its right or below.
    /// @note A degenerate extent (a side <= 0) leaves the ratio undefined, so the point is returned
    ///       UNCHANGED: never a division by zero, and never the origin, which would put the pointer in
    ///       a corner of a window whose size is not known yet. The identity is also what a density of
    ///       1 produces, so the fallback cannot move a pointer that was correct without it.
    /// @note The result is saturated at the int range; only a point far outside the client area can
    ///       reach that.
    /// @note noexcept and constexpr: the conversion is pure arithmetic, so it is unit-tested at
    ///       densities the display it runs on does not report.
    constexpr point_t window_to_pixel_point(point_t window_point, extent_t window_size, extent_t pixel_size) noexcept {
        if (window_size.isEmpty() || pixel_size.isEmpty()) {
            return window_point;
        }
        return point_t{
            detail::scale_coordinate(window_point.x(), window_size.width(), pixel_size.width()),
            detail::scale_coordinate(window_point.y(), window_size.height(), pixel_size.height()),
        };
    }
}

#endif
