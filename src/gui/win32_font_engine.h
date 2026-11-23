#ifndef HWSHQTB__GUI__WIN32_FONT_ENGINE_H
#define HWSHQTB__GUI__WIN32_FONT_ENGINE_H

/// @file win32_font_engine.h
/// @brief Internal: the GDI font engine (the QWindowsFontEngine role).
///
/// @par No Win32 type in this header
/// The class holds one opaque `struct impl` pointer, so no HWND/HDC/HFONT/HGDIOBJ and no windows.h
/// appears here; the Win32 headers live in the .cpp only, exactly like sdl_platform_window.
///
/// @par Glyph identity is the code point
/// GDI's GetGlyphOutlineW takes the CHARACTER to draw, not a glyph index ("uChar: the character for
/// which data is to be returned"). On a face with embedded bitmap strikes — the system UI face
/// Microsoft YaHei UI at lfHeight -12 is one — GDI treats the face as a raster font and answers for the
/// character, so the glyph indices of GetGlyphIndicesW and the argument of GetGlyphOutlineW are NOT the
/// same space: asking GGO for the index that maps 'A' (39) drew an apostrophe (1x3 box, 3 px advance),
/// while GGO('A') gave the real 8x9 box and the 8 px advance that GetTextExtentPoint32W and
/// GetCharABCWidthsW both report.
/// This engine therefore keeps ONE space: its glyph index IS the code point. glyph_index() only answers
/// "can GDI draw this character", and advance()/glyph_bitmap() hand the value straight back to GDI. A
/// composite (fallback) engine may pack a sub-engine number beside it, because the character is
/// unpacked before GDI ever sees it.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "hwshqtb/gui/font_engine_t.h"

namespace hwshqtb::gui::detail {
    /// @brief The face Windows uses for its own UI (SPI_GETNONCLIENTMETRICS.lfMessageFont), or "".
    /// @note Used as the default family, so "the application default font" is the one the system uses.
    ///       An empty answer means the caller stays on the built-in face.
    std::string system_default_family();

    /// @brief The pixel height the system UI face asks for (-lfMessageFont.lfHeight), or 0.
    int system_default_pixel_size();

    /// @brief The GDI font engine: metrics, character mapping and anti-aliased glyph masks.
    ///
    /// @par One DC, one font, one cache
    /// The implementation keeps a memory DC with the face selected for its whole life and caches one
    /// 8-bit mask per rendered character.
    ///
    /// @par Failure is per glyph, never global
    /// A character GDI cannot draw answers false from glyph_index()/glyph_bitmap(), and the layout draws
    /// the missing-glyph box. Only a face that cannot be created at all throws, and the font database
    /// then falls back to the built-in face, so text never disappears.
    class win32_font_engine final: public font_engine_t {
    public:
        /// @brief Create the engine for @p family at @p pixel_size pixels.
        /// @param family The face name; empty means the system UI face.
        /// @param pixel_size The requested pixel height; 0 means the system's size, and an absurd value
        ///        is clamped to a readable one.
        /// @throws std::runtime_error when no device context or face can be created.
        explicit win32_font_engine(std::string_view family, int pixel_size);

        ~win32_font_engine() override;

        win32_font_engine(const win32_font_engine&) = delete;
        win32_font_engine& operator=(const win32_font_engine&) = delete;

        int ascent() const noexcept override;
        int descent() const noexcept override;
        int leading() const noexcept override;
        int advance(std::uint32_t glyph) const noexcept override;
        bool glyph_index(char32_t codepoint, std::uint32_t& glyph) const noexcept override;
        bool glyph_bitmap(std::uint32_t glyph, glyph_bitmap_t& out) const override;

        /// @brief The face GDI actually selected (a substitution shows up here).
        std::string_view face_name() const noexcept;

        /// @brief The pixel height the engine was built for.
        int pixel_size() const noexcept;

    private:
        /// @brief The Win32 state (DC, HFONT, metrics, caches); defined in the .cpp.
        struct impl;
        /// @brief The Win32 state.
        std::unique_ptr<impl> _impl;
    };
}

#endif
