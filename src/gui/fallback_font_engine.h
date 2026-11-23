#ifndef HWSHQTB__GUI__FALLBACK_FONT_ENGINE_H
#define HWSHQTB__GUI__FALLBACK_FONT_ENGINE_H

/// @file fallback_font_engine.h
/// @brief Internal: the per-character fallback chain over several font engines (the QFontEngineMulti
///        role).

#include <cstdint>
#include <vector>

#include "hwshqtb/gui/font_engine_t.h"

namespace hwshqtb::gui::detail {
    /// @brief Several engines asked in order for each character: the one that has the glyph answers.
    ///
    /// @par Why a chain
    /// A face covers a subset of the code points: a Latin UI face usually has no CJK glyphs, and the
    /// built-in bitmap face has only printable ASCII. Asking one engine is therefore not enough — a
    /// layout has to try the primary face, then the fallbacks, and only then give up and draw the
    /// missing-glyph box. That is QFontEngineMulti's job, and this is it.
    ///
    /// @par Glyph encoding
    /// A glyph index of this engine packs the sub-engine number beside the inner glyph:
    /// @c (index_of_child + 1) << 24 | inner_glyph. The offset of one keeps index 0 free: it always means
    /// "the missing-glyph box", which is the LAST child's glyph 0 (the built-in face, whose box is the
    /// one the whole project draws). A child glyph must therefore fit in 24 bits — a code point does
    /// (U+10FFFF), and so does any real font's glyph count.
    ///
    /// @par Metrics
    /// ascent/descent/leading come from the PRIMARY child: a line has one baseline grid even when its
    /// characters resolve through different faces.
    class fallback_font_engine_t final: public font_engine_t {
    public:
        /// @brief Build the chain.
        /// @param engines The children, primary first and the built-in face last (at least one).
        /// @throws std::invalid_argument when @p engines is empty.
        /// @note The children are not owned and must outlive this engine (the font database owns both).
        explicit fallback_font_engine_t(std::vector<font_engine_t*> engines);

        ~fallback_font_engine_t() override;

        fallback_font_engine_t(const fallback_font_engine_t&) = delete;
        fallback_font_engine_t& operator=(const fallback_font_engine_t&) = delete;

        int ascent() const noexcept override;
        int descent() const noexcept override;
        int leading() const noexcept override;
        int advance(std::uint32_t glyph) const noexcept override;
        bool glyph_index(char32_t codepoint, std::uint32_t& glyph) const noexcept override;
        bool glyph_bitmap(std::uint32_t glyph, glyph_bitmap_t& out) const override;

        /// @brief How many engines the chain asks, the primary included.
        /// @note noexcept.
        std::size_t engine_count() const noexcept;

        /// @brief The bits of a packed glyph index that hold the inner glyph.
        static constexpr std::uint32_t inner_glyph_mask = 0x00FFFFFFu;

    private:
        /// @brief The children, primary first.
        std::vector<font_engine_t*> _engines;
    };
}

#endif
