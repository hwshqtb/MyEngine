/// @file detail/builtin_font_engine.cpp
/// @brief The font engine of the built-in 5x7 bitmap face.

#include "builtin_font_engine.h"

#include <cstddef>

namespace hwshqtb::gui::detail {
    namespace {
        /// @brief printf a single mask row into the 8-bit expansion of @p masks.
        void expand_row(const std::uint8_t* rows, std::uint8_t* destination) noexcept {
            for (int column = 0; column < builtin_glyph_width; ++column) {
                const bool ink = ((rows[0] >> (builtin_glyph_width - 1 - column)) & 1u) != 0;
                destination[column] = ink ? 0xFFu : 0x00u;
            }
        }
    }

    builtin_font_engine::builtin_font_engine() {
        // One mask per glyph: index 0 is the missing-glyph box, 1..95 are code points 32..126.
        _masks.assign(static_cast<std::size_t>(builtin_glyph_count) * builtin_glyph_width * builtin_glyph_height, 0);
        for (std::uint32_t glyph = 0; glyph < builtin_glyph_count; ++glyph) {
            const auto rows = this->rows(glyph);
            for (int row = 0; row < builtin_glyph_height; ++row) {
                std::uint8_t* const destination = _masks.data()
                    + (static_cast<std::size_t>(glyph) * builtin_glyph_height + static_cast<std::size_t>(row))
                        * builtin_glyph_width;
                expand_row(&rows[static_cast<std::size_t>(row)], destination);
            }
        }
    }

    builtin_font_engine::~builtin_font_engine() = default;

    int builtin_font_engine::ascent() const noexcept {
        return 6;
    }

    int builtin_font_engine::descent() const noexcept {
        return 1;
    }

    int builtin_font_engine::leading() const noexcept {
        return 1;
    }

    int builtin_font_engine::advance(std::uint32_t glyph) const noexcept {
        // Monospace: the space, a control code point and the missing box all take one cell.
        (void)glyph;
        return advance_columns;
    }

    bool builtin_font_engine::glyph_index(char32_t codepoint, std::uint32_t& glyph) const noexcept {
        if (codepoint < static_cast<char32_t>(builtin_first_codepoint)
            || codepoint > static_cast<char32_t>(builtin_last_codepoint)) {
            return false;
        }
        glyph = static_cast<std::uint32_t>(codepoint) - builtin_first_codepoint + 1u;
        return true;
    }

    bool builtin_font_engine::glyph_bitmap(std::uint32_t glyph, glyph_bitmap_t& out) const {
        if (glyph >= builtin_glyph_count) {
            return false;
        }
        out.alpha = _masks.data() + static_cast<std::size_t>(glyph) * builtin_glyph_width * builtin_glyph_height;
        out.width = builtin_glyph_width;
        out.height = builtin_glyph_height;
        out.pitch = builtin_glyph_width;
        out.antialiased = false;
        // The 5x7 cell is monospaced and the ink starts at the cell's left edge, so there is no bearing:
        // this is what keeps every built-in-face pixel where it always was.
        out.bearing_x = 0;
        return true;
    }

    std::array<std::uint8_t, builtin_glyph_height> builtin_font_engine::rows(std::uint32_t glyph) const noexcept {
        // The legacy row accessor is defined over the TABLE, not over the constructed masks, so it also
        // works while the constructor is filling them.
        if (glyph == builtin_missing_glyph || glyph >= builtin_glyph_count) {
            return builtin_glyph_rows(-1);
        }
        return builtin_glyph_rows(builtin_first_codepoint + static_cast<int>(glyph) - 1);
    }
}
