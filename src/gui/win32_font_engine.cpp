/// @file win32_font_engine.cpp
/// @brief The GDI font engine. Every Win32 type and header lives in this translation unit.
///
/// @par The five measured facts this file is built on
/// 1. GetGlyphOutlineW's second argument is the CHARACTER, not a glyph index (see the header): this
///    engine's glyph index IS the code point.
/// 2. Its MAT2 argument cannot be NULL: every format answers GDI_ERROR for a null matrix, and the
///    identity matrix is what makes GGO_METRICS/GGO_GRAY4_BITMAP work here.
/// 3. A glyph bitmap is one byte per pixel, every row padded to four bytes.
/// 4. Its rows are TOP-DOWN: row 0 is the top of the glyph (the crossbar of 'A' lands at about two
///    thirds of the box from row 0, which is only true reading downwards).
/// 5. GGO_GRAY4_BITMAP reports coverage in 0..16, so expanding to the interface's 8-bit alpha is
///    value * 255 / 16.

#include "win32_font_engine.h"

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// <windows.h> defines CONST as a macro, which collides with the metaobject enumerators of any header
// included after it. Nothing here uses the Win32 spelling.
#ifdef CONST
#undef CONST
#endif
#endif

namespace hwshqtb::gui::detail {
#if defined(_WIN32)
    namespace {
        /// @brief The identity FIXED matrix GetGlyphOutlineW demands (fact 2).
        const MAT2& identity_mat2() noexcept {
            static const MAT2 identity = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };
            return identity;
        }

        /// @brief One cached mask: 8-bit coverage over the whole line box, top row first.
        struct mask_t {
            /// @brief Coverage, @c pitch bytes per row; empty means "this glyph has no pixels".
            std::vector<std::uint8_t> alpha;
            /// @brief The ink width in pixels.
            int width = 0;
            /// @brief The line-box height in pixels (ascent + descent).
            int height = 0;
            /// @brief Bytes per row; a multiple of four (fact 3).
            int pitch = 0;
            /// @brief The left side bearing, kept with the mask so a cache hit answers it too.
            int bearing_x = 0;
        };

        /// @brief The smallest pixel size the engine honours.
        constexpr int minimum_pixel_size = 6;
        /// @brief The largest pixel size the engine honours (a typo must not allocate gigabytes).
        constexpr int maximum_pixel_size = 512;

        /// @brief Narrow a wide face name for this std::string-based API.
        std::string narrow_face(const wchar_t* wide) {
            std::string narrow;
            for (const wchar_t* cursor = wide; cursor != nullptr && *cursor != L'\0'; ++cursor) {
                narrow.push_back(*cursor < 0x80 ? static_cast<char>(*cursor) : '?');
            }
            return narrow;
        }
    }

    struct win32_font_engine::impl {
        /// @brief The memory DC every GDI call goes through.
        HDC dc = nullptr;
        /// @brief The selected face.
        HFONT font = nullptr;
        /// @brief The face GDI actually selected.
        std::string face;
        /// @brief The requested pixel height.
        int pixel_size = 0;
        /// @brief tmAscent of the selected face.
        int ascent = 0;
        /// @brief tmDescent of the selected face.
        int descent = 0;
        /// @brief tmExternalLeading of the selected face.
        int leading = 0;
        /// @brief Advances by character, memoised (GGO_METRICS answers per character).
        std::map<std::uint32_t, int> advances;
        /// @brief Masks by character; the map keeps node addresses stable, so handed-out pointers live
        ///        as long as the engine does.
        std::map<std::uint32_t, mask_t> masks;
        /// @brief Set when the DC or the face could not be created: every answer is then "no glyph".
        bool failed = false;

        ~impl() {
            if (dc != nullptr) {
                if (font != nullptr) {
                    (void)SelectObject(dc, GetStockObject(SYSTEM_FONT));
                }
                DeleteDC(dc);
            }
            if (font != nullptr) {
                DeleteObject(font);
            }
        }
    };

    std::string system_default_family() {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            return {};
        }
        return narrow_face(metrics.lfMessageFont.lfFaceName);
    }

    int system_default_pixel_size() {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            return 0;
        }
        // lfHeight is negative for "character height", which is the pixel size we want.
        const int height = metrics.lfMessageFont.lfHeight < 0 ? -metrics.lfMessageFont.lfHeight
                                                              : metrics.lfMessageFont.lfHeight;
        return height > 0 ? height : 0;
    }

    win32_font_engine::win32_font_engine(std::string_view family, int pixel_size):
        _impl(std::make_unique<impl>()) {
        const std::string requested = family.empty() ? system_default_family() : std::string(family);
        if (requested.empty()) {
            throw std::runtime_error("win32_font_engine: no face name");
        }
        int height = pixel_size > 0 ? pixel_size : system_default_pixel_size();
        if (height <= 0) {
            height = 12; // a readable default when the system refuses to say
        }
        height = height < minimum_pixel_size ? minimum_pixel_size : height;
        height = height > maximum_pixel_size ? maximum_pixel_size : height;
        _impl->pixel_size = height;

        _impl->dc = CreateCompatibleDC(nullptr);
        if (_impl->dc == nullptr) {
            _impl->failed = true;
            throw std::runtime_error("win32_font_engine: CreateCompatibleDC failed");
        }
        // One logical unit is one pixel, so a requested pixel height is a pixel height.
        (void)SetMapMode(_impl->dc, MM_TEXT);
        const std::wstring wide_face(requested.begin(), requested.end());
        // A negative lfHeight means "character height", which is what a pixel size is.
        _impl->font = CreateFontW(-height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, wide_face.c_str());
        if (_impl->font == nullptr) {
            _impl->failed = true;
            throw std::runtime_error("win32_font_engine: CreateFontW failed for " + requested);
        }
        HGDIOBJ const previous = SelectObject(_impl->dc, _impl->font);
        if (previous == nullptr || previous == HGDI_ERROR) {
            _impl->failed = true;
            throw std::runtime_error("win32_font_engine: SelectObject failed for " + requested);
        }
        TEXTMETRICW metrics{};
        if (!GetTextMetricsW(_impl->dc, &metrics)) {
            _impl->failed = true;
            throw std::runtime_error("win32_font_engine: GetTextMetricsW failed for " + requested);
        }
        _impl->ascent = metrics.tmAscent;
        _impl->descent = metrics.tmDescent;
        _impl->leading = metrics.tmExternalLeading;
        // The face GDI actually selected: a substitution (a missing family) shows up here rather than
        // failing, which is why the engine reports it.
        wchar_t actual[LF_FACESIZE] = { 0 };
        if (GetTextFaceW(_impl->dc, LF_FACESIZE, actual) > 0) {
            _impl->face = narrow_face(actual);
        }
    }

    win32_font_engine::~win32_font_engine() = default;

    int win32_font_engine::ascent() const noexcept {
        return _impl->ascent;
    }

    int win32_font_engine::descent() const noexcept {
        return _impl->descent;
    }

    int win32_font_engine::leading() const noexcept {
        return _impl->leading;
    }

    std::string_view win32_font_engine::face_name() const noexcept {
        return _impl->face;
    }

    int win32_font_engine::pixel_size() const noexcept {
        return _impl->pixel_size;
    }

    int win32_font_engine::advance(std::uint32_t glyph) const noexcept {
        if (_impl->failed) {
            return 0;
        }
        const auto cached = _impl->advances.find(glyph);
        if (cached != _impl->advances.end()) {
            return cached->second;
        }
        // fact 1: the argument is the character, and this engine's glyph index IS the character.
        GLYPHMETRICS metrics{};
        const DWORD size = GetGlyphOutlineW(_impl->dc, static_cast<UINT>(glyph), GGO_METRICS, &metrics, 0,
                                            nullptr, &identity_mat2());
        const int result = size == GDI_ERROR ? 0 : static_cast<int>(metrics.gmCellIncX);
        _impl->advances.emplace(glyph, result);
        return result;
    }

    bool win32_font_engine::glyph_index(char32_t codepoint, std::uint32_t& glyph) const noexcept {
        if (_impl->failed) {
            return false;
        }
        // GDI maps UTF-16 code units, and GetGlyphOutlineW takes one character: anything outside the BMP
        // (and the control range) has no glyph here, and the layout draws the missing-glyph box.
        if (codepoint < 0x20 || codepoint > 0xFFFF) {
            return false;
        }
        const wchar_t character = static_cast<wchar_t>(codepoint);
        WORD index = 0xFFFF;
        if (GetGlyphIndicesW(_impl->dc, &character, 1, &index, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) {
            return false;
        }
        if (index == 0xFFFF) {
            return false; // GDI says this face has no glyph for it
        }
        // fact 1 again: the identity mapping IS the contract of this engine.
        glyph = static_cast<std::uint32_t>(codepoint);
        return true;
    }

    bool win32_font_engine::glyph_bitmap(std::uint32_t glyph, glyph_bitmap_t& out) const {
        if (_impl->failed) {
            return false;
        }
        const auto cached = _impl->masks.find(glyph);
        if (cached != _impl->masks.end()) {
            if (cached->second.alpha.empty()) {
                return false;
            }
            out.alpha = cached->second.alpha.data();
            out.width = cached->second.width;
            out.height = cached->second.height;
            out.pitch = cached->second.pitch;
            out.antialiased = true;
            out.bearing_x = cached->second.bearing_x;
            return true;
        }
        mask_t mask;
        GLYPHMETRICS metrics{};
        const DWORD size = GetGlyphOutlineW(_impl->dc, static_cast<UINT>(glyph), GGO_GRAY4_BITMAP, &metrics, 0,
                                            nullptr, &identity_mat2());
        if (size == GDI_ERROR || size == 0 || metrics.gmBlackBoxX == 0 || metrics.gmBlackBoxY == 0) {
            // No pixels (a space) or GDI refused: remember the answer, draw nothing for this character.
            _impl->masks.emplace(glyph, std::move(mask));
            return false;
        }
        std::vector<std::uint8_t> raw(size, 0);
        if (GetGlyphOutlineW(_impl->dc, static_cast<UINT>(glyph), GGO_GRAY4_BITMAP, &metrics, size, raw.data(),
                             &identity_mat2()) == GDI_ERROR) {
            _impl->masks.emplace(glyph, std::move(mask));
            return false;
        }
        const int ink_width = static_cast<int>(metrics.gmBlackBoxX);
        const int ink_height = static_cast<int>(metrics.gmBlackBoxY);
        // facts 3 and 4: one byte per pixel, rows padded to four, first row is the top row.
        const int source_pitch = (ink_width + 3) & ~3;
        // The mask is the whole LINE BOX, with the ink where GDI puts it relative to the baseline:
        // origin.y rows above it are the glyph's top. The paint engine draws a mask at pen.y - ascent,
        // so a line-box mask needs no per-glyph bearing in the interface.
        const int box_height = _impl->ascent + _impl->descent;
        const int top = _impl->ascent - static_cast<int>(metrics.gmptGlyphOrigin.y);
        mask.width = ink_width;
        mask.height = box_height > 0 ? box_height : ink_height;
        mask.pitch = (mask.width + 3) & ~3;
        mask.alpha.assign(static_cast<std::size_t>(mask.pitch) * static_cast<std::size_t>(mask.height), 0);
        for (int row = 0; row < ink_height; ++row) {
            const int destination_row = top + row;
            if (destination_row < 0 || destination_row >= mask.height) {
                continue; // ink outside the line box (a very tall glyph): clipped, never fatal
            }
            const std::uint8_t* const source = raw.data() + static_cast<std::ptrdiff_t>(row) * source_pitch;
            std::uint8_t* const destination = mask.alpha.data()
                + static_cast<std::ptrdiff_t>(destination_row) * mask.pitch;
            for (int column = 0; column < ink_width; ++column) {
                const std::uint32_t coverage = source[column];
                if (coverage == 0) {
                    continue;
                }
                // fact 5: GRAY4 reports 0..16 levels; the interface promises 0..255.
                destination[column] = static_cast<std::uint8_t>(coverage >= 16u ? 255u : coverage * 255u / 16u);
            }
        }
        // GDI's gmptGlyphOrigin.x IS the left side bearing: how far right of the pen the ink starts.
        // (The vertical component is already baked into the line-box mask above.)
        mask.bearing_x = static_cast<int>(metrics.gmptGlyphOrigin.x);
        const mask_t& stored = _impl->masks.emplace(glyph, std::move(mask)).first->second;
        out.alpha = stored.alpha.data();
        out.width = stored.width;
        out.height = stored.height;
        out.pitch = stored.pitch;
        out.antialiased = true;
        out.bearing_x = stored.bearing_x;
        return true;
    }
#else
    namespace {
        /// @brief Placeholder state on a platform with no GDI.
        struct unsupported_impl_t { };
    }

    struct win32_font_engine::impl: unsupported_impl_t { };

    std::string system_default_family() {
        return {};
    }

    int system_default_pixel_size() {
        return 0;
    }

    win32_font_engine::win32_font_engine(std::string_view, int):
        _impl(std::make_unique<impl>()) {
        throw std::runtime_error("win32_font_engine: no platform font backend on this platform");
    }

    win32_font_engine::~win32_font_engine() = default;

    int win32_font_engine::ascent() const noexcept {
        return 0;
    }

    int win32_font_engine::descent() const noexcept {
        return 0;
    }

    int win32_font_engine::leading() const noexcept {
        return 0;
    }

    int win32_font_engine::advance(std::uint32_t) const noexcept {
        return 0;
    }

    bool win32_font_engine::glyph_index(char32_t, std::uint32_t&) const noexcept {
        return false;
    }

    bool win32_font_engine::glyph_bitmap(std::uint32_t, glyph_bitmap_t&) const {
        return false;
    }

    std::string_view win32_font_engine::face_name() const noexcept {
        return {};
    }

    int win32_font_engine::pixel_size() const noexcept {
        return 0;
    }
#endif
}
