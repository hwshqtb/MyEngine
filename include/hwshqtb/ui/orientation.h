#ifndef HWSHQTB__UI__ORIENTATION_H
#define HWSHQTB__UI__ORIENTATION_H

/// @file orientation.h
/// @brief orientation_enum: the one long-axis direction vocabulary of the ui layer (progress bar,
///        slider, scroll bar).

#include <cstdint>

namespace hwshqtb::ui {
    /// @brief Along which axis a control's content grows and its handle travels.
    ///
    /// @par One vocabulary
    /// A progress bar, a slider and a scroll bar all run along an axis and all mean the same two
    /// directions, so they share this enum instead of each declaring its own copy.
    ///
    /// @par Meaning
    /// HORIZONTAL: the minimum is at the left and the maximum at the right.
    /// VERTICAL: "left/right" become "top/bottom"; each control documents which end is its minimum —
    /// a slider and a progress bar grow UPWARDS (minimum at the bottom), a scroll bar grows
    /// DOWNWARDS (minimum at the top, following the content it scrolls).
    enum struct orientation_enum : std::uint8_t {
        /// @brief The long axis runs left to right.
        HORIZONTAL,
        /// @brief The long axis runs top to bottom.
        VERTICAL,
    };
}

#endif
