#ifndef HWSHQTB__GUI__TEXT_LAYOUT_H
#define HWSHQTB__GUI__TEXT_LAYOUT_H

/// @file text_layout_t.h
/// @brief text_layout_t: code points → positioned glyph runs (the QTextLayout / QTextEngine role).

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/paint_engine_t.h"

namespace hwshqtb::gui {
    /// @brief The laid-out form of one string: positioned glyph runs plus the extent they occupy.
    ///
    /// @par What it owns
    /// The layout decodes the text ONCE, resolves every code point to a glyph index through the font
    /// database, and stores the glyph indices and pen positions in its own buffers. runs() hands out
    /// glyph_run_t values whose pointers refer to those buffers, so a caller may pass them straight to
    /// paint_engine_t::draw_glyphs() and must not keep them after the layout dies.
    ///
    /// @par Runs follow the engine
    /// A run never mixes engines: every contiguous stretch of text that resolves through the same
    /// font_engine_t is one run. With a single engine (the built-in face) a whole multi-line string is
    /// one run whose positions carry the line baselines; a fallback chain produces one run per segment,
    /// which is exactly what QFontEngineMulti does (Qt's QGlyphRun equivalent).
    ///
    /// @par One code point, one cell
    /// The unit of layout is the CODE POINT, not the byte: "中" is one glyph and one advance, whether it
    /// is one byte or three. A malformed byte is one replacement glyph. '\n' moves the pen to the next
    /// baseline and never reaches an engine.
    ///
    /// @par Extent
    /// width() is the widest line's summed advances, height() is (number of lines) * line_height(). The
    /// empty string has an empty extent, the same answer font_metrics_t::text_extent() gives.
    class HWSHQTB_GUI_EXPORT text_layout_t {
    public:
        /// @brief Lay @p text out in @p font.
        /// @param text The UTF-8 text; '\n' separates lines.
        /// @param font The description; resolved through font_database_t.
        /// @throws std::runtime_error when the platform font backend fails in a way the database cannot
        ///         fall back from (it falls back to the built-in face for every recoverable failure).
        text_layout_t(std::string_view text, const font_t& font);

        ~text_layout_t();

        text_layout_t(const text_layout_t&) = delete;
        text_layout_t& operator=(const text_layout_t&) = delete;
        text_layout_t(text_layout_t&& other) noexcept;
        text_layout_t& operator=(text_layout_t&& other) noexcept;

        /// @brief The positioned runs, in text order.
        /// @return The runs; empty for an empty string and for a string of newlines only. The pointers
        ///         inside them are valid until this layout is destroyed or translated.
        /// @note noexcept.
        std::span<const glyph_run_t> runs() const noexcept;

        /// @brief The logical extent of the laid-out text.
        /// @note noexcept.
        extent_t extent() const noexcept;

        /// @brief The baseline-to-baseline distance of the layout's font.
        /// @note noexcept.
        int line_height() const noexcept;

        /// @brief Shift every pen position by @p offset.
        /// @param offset The offset in pixels.
        /// @note This is the painter's translate fallback for a backend without
        ///       engine_feature_flag::TRANSLATE: it moves the already-laid-out glyphs, which is cheaper
        ///       and more exact than re-laying them out at a different origin.
        /// @note noexcept. The runs() pointers stay valid (the storage is not reallocated).
        void translate(point_t offset) noexcept;

    private:
        /// @brief The laid-out data: one segment per engine plus the runs over it.
        struct impl;
        /// @brief The laid-out data.
        std::unique_ptr<impl> _impl;
    };
}

#endif
