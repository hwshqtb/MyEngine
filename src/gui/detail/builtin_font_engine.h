#ifndef HWSHQTB__GUI__DETAIL__BUILTIN_FONT_ENGINE_H
#define HWSHQTB__GUI__DETAIL__BUILTIN_FONT_ENGINE_H

/// @file detail/builtin_font_engine.h
/// @brief Internal: the font engine of the built-in 5x7 bitmap face.

#include <cstdint>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/font_engine_t.h"
#include "builtin_glyph_table.h"

namespace hwshqtb::gui::detail {
    /// @brief The font engine over the built-in 5x7 bitmap glyph table.
    ///
    /// It is the deterministic fallback every other engine chain ends with: no platform, no display and
    /// no allocation beyond one mask buffer, so a test or a golden capture can pin exact pixels. Its
    /// masks are produced once in the constructor (an 8-bit expansion of the bit rows), so
    /// glyph_bitmap() never allocates and every returned pointer is stable.
    ///
    /// @par Coverage
    /// Printable ASCII (32..126) only. Every other code point has no glyph: glyph_index() answers
    /// false, the layout substitutes the missing-glyph box (index 0), and advance() stays the
    /// monospace 6 pixels, so an unsupported character still occupies exactly one cell.
    class builtin_font_engine final: public font_engine_t {
    public:
        /// @brief The family name of the built-in face: what a test pins to get deterministic text.
        static constexpr std::string_view family_name = "hwshqtb-mono-5x7";

        /// @brief The monospace advance: the five ink columns plus one spacing column.
        static constexpr int advance_columns = builtin_glyph_width + 1;

        /// @brief Build the mask cache (one 8-bit mask per glyph, the box included).
        builtin_font_engine();

        ~builtin_font_engine() override;

        builtin_font_engine(const builtin_font_engine&) = delete;
        builtin_font_engine& operator=(const builtin_font_engine&) = delete;

        /// @brief The rows above the baseline (6).
        int ascent() const noexcept override;

        /// @brief The rows below the baseline (1).
        int descent() const noexcept override;

        /// @brief The blank rows between baselines (1).
        int leading() const noexcept override;

        /// @brief The monospace advance (6) for every glyph, the missing box included.
        int advance(std::uint32_t glyph) const noexcept override;

        /// @brief Map a printable ASCII code point to its glyph index.
        /// @return true for 32..126; false for anything else (including every non-ASCII code point).
        bool glyph_index(char32_t codepoint, std::uint32_t& glyph) const noexcept override;

        /// @brief The 5x7 mask of one glyph.
        /// @return true when @p glyph is inside the table (index 0 is always the box).
        bool glyph_bitmap(std::uint32_t glyph, glyph_bitmap_t& out) const override;

        /// @brief The seven row bytes of one glyph, for the legacy row accessor.
        /// @param glyph The glyph index; out-of-range answers the box.
        std::array<std::uint8_t, builtin_glyph_height> rows(std::uint32_t glyph) const noexcept;

    private:
        /// @brief One 8-bit mask per glyph, laid out pitch = width = 5, height = 7.
        std::vector<std::uint8_t> _masks;
    };
}

#endif
