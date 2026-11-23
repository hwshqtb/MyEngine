#ifndef HWSHQTB__GUI__FONT_ENGINE_H
#define HWSHQTB__GUI__FONT_ENGINE_H

/// @file font_engine_t.h
/// @brief font_engine_t: the rasteriser behind a font (the QFontEngine role).

#include <cstdint>

#include "hwshqtb/gui/export.h"

namespace hwshqtb::gui {
    /// @brief One rasterised glyph as an 8-bit alpha mask (a QFontEngine glyph bitmap).
    ///
    /// @par Format
    /// @c alpha points at @c pitch * @c height bytes of 8-bit coverage, top row first: 0 is fully
    /// transparent and 255 fully opaque. The backend expands whatever its native format is into this
    /// one, so an upper layer never asks "is this 1bpp or 4bpp?".
    ///
    /// @par Lifetime
    /// The pointer stays valid until the engine that produced it destroys its cache (for this project:
    /// for the engine's whole lifetime). A caller must not write through it.
    struct glyph_bitmap_t {
        /// @brief 8-bit coverage, top row first.
        const std::uint8_t* alpha = nullptr;
        /// @brief The bitmap width in pixels.
        int width = 0;
        /// @brief The bitmap height in pixels.
        int height = 0;
        /// @brief Bytes per row (at least @c width).
        int pitch = 0;
        /// @brief Whether the coverage has intermediate values (a hint for scaling and hinting).
        bool antialiased = false;
        /// @brief The left side bearing: how far RIGHT of the pen the mask's first column starts.
        /// @note Negative for a glyph that overhangs its origin (an italic 'j', a kerned pair). The
        ///       VERTICAL offset is deliberately NOT a field: the engine bakes it into the mask (the
        ///       mask already covers the line box), so a renderer places the mask at
        ///       `pen.y - engine->ascent()` and only the horizontal bearing has to travel with the mask.
        int bearing_x = 0;
    };

    /// @brief The rasteriser behind one resolved font: metrics, character mapping and glyph bitmaps.
    ///
    /// This is the QFontEngine role: font_t is a pure DESCRIPTION (family + pixel size), the font
    /// database resolves it to an engine, and the engine answers the metrics and hands out glyph
    /// masks. A text layout asks for a glyph index per code point and stores that index in the glyph
    /// run; a paint engine turns the index into pixels.
    ///
    /// @par Glyph indices
    /// A glyph index is the engine's own character-to-glyph mapping result, NOT a code point. Index 0
    /// is the engine's missing-glyph (the box a layout draws for a code point the engine cannot map).
    ///
    /// @par Lifetime
    /// Engines belong to the font database and live for the process's life; a caller never creates or
    /// destroys one.
    class HWSHQTB_GUI_EXPORT font_engine_t {
    public:
        virtual ~font_engine_t();

        font_engine_t(const font_engine_t&) = delete;
        font_engine_t& operator=(const font_engine_t&) = delete;
        font_engine_t(font_engine_t&&) = delete;
        font_engine_t& operator=(font_engine_t&&) = delete;

        /// @brief The number of pixels above the baseline.
        /// @note noexcept.
        virtual int ascent() const noexcept = 0;

        /// @brief The number of pixels below the baseline.
        /// @note noexcept.
        virtual int descent() const noexcept = 0;

        /// @brief The extra pixels between two baselines besides ascent + descent.
        /// @note noexcept.
        virtual int leading() const noexcept = 0;

        /// @brief The pen advance of one glyph, in pixels.
        /// @param glyph The glyph index.
        /// @note noexcept. A missing glyph answers the advance of index 0.
        virtual int advance(std::uint32_t glyph) const noexcept = 0;

        /// @brief Map one code point to a glyph index.
        /// @param codepoint The code point to map.
        /// @param glyph Receives the glyph index on success.
        /// @return true when this engine has a glyph for @p codepoint; false when the layout must fall
        ///         back to another engine or draw the missing-glyph box.
        /// @note noexcept. A pure query: it never rasterises anything.
        virtual bool glyph_index(char32_t codepoint, std::uint32_t& glyph) const noexcept = 0;

        /// @brief Rasterise one glyph.
        /// @param glyph The glyph index.
        /// @param out Receives the mask on success.
        /// @return true when the glyph has pixels; false when it has none (the caller draws nothing
        ///         for it — a space, for example).
        /// @note Logically const: it fills the engine's own glyph cache and never changes what the
        ///       engine MEANS, which is why a glyph run can hold a `const font_engine_t*` and still ask
        ///       for pixels. An implementation makes its cache `mutable`.
        virtual bool glyph_bitmap(std::uint32_t glyph, glyph_bitmap_t& out) const = 0;

    protected:
        /// @brief Constructor; only an engine implementation (or the font database) may create one.
        font_engine_t() = default;
    };
}

#endif
