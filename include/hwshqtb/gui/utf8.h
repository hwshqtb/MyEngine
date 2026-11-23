#ifndef HWSHQTB__GUI__UTF8_H
#define HWSHQTB__GUI__UTF8_H

/// @file utf8.h
/// @brief The one UTF-8 decoder of the GUI layer: codepoints, boundaries and code-point counts.
///
/// @par Why header-only
/// The decoder is pure integer arithmetic and must be usable in constant expressions (its rounding,
/// saturation and invalid-byte rules are pinned with static_assert in the tests), so its definitions
/// live here — the same exception to "declarations in headers" as geometry.h and window_coordinate.h.
/// It is the ONE decoder: the text layout, the metrics and the line edit all call these functions
/// instead of each keeping its own byte table.

#include <cstddef>
#include <string_view>

namespace hwshqtb::gui {
    /// @brief The result of a malformed sequence: utf8_next() answers -1 and advances one byte.
    inline constexpr int utf8_invalid = -1;

    /// @brief The length in bytes of the UTF-8 sequence whose lead byte is @p lead.
    /// @param lead The leading byte.
    /// @return 1..4 for a valid lead byte, 0 when @p lead cannot start a sequence (a stray continuation
    ///         byte or 0xF8..0xFF).
    /// @note noexcept and constexpr.
    constexpr int utf8_sequence_length(unsigned char lead) noexcept {
        if (lead < 0x80) {
            return 1;
        }
        if ((lead & 0xE0) == 0xC0) {
            return 2;
        }
        if ((lead & 0xF0) == 0xE0) {
            return 3;
        }
        if ((lead & 0xF8) == 0xF0) {
            return 4;
        }
        return 0;
    }

    /// @brief Decode the code point starting at @p index and advance past it.
    /// @param text The UTF-8 text.
    /// @param index In: the byte offset to decode. Out: past the sequence — one byte further for a
    ///        malformed sequence, so a decoder loop always makes progress.
    /// @return The code point, or utf8_invalid (-1) when the bytes at @p index are not a well-formed
    ///        UTF-8 sequence (bad lead byte, bad continuation byte, truncated tail, overlong form or a
    ///        value above U+10FFFF). An invalid result means "draw one replacement box, consume one
    ///        byte".
    /// @note noexcept and constexpr.
    constexpr int utf8_next(std::string_view text, std::size_t& index) noexcept {
        if (index >= text.size()) {
            return utf8_invalid;
        }
        const auto byte_at = [text](std::size_t position) {
            return static_cast<unsigned char>(text[position]);
        };
        const unsigned char lead = byte_at(index);
        const int length = utf8_sequence_length(lead);
        if (length == 0 || index + static_cast<std::size_t>(length) > text.size()) {
            ++index;
            return utf8_invalid;
        }
        // Continuation bytes must all be 10xxxxxx, or the sequence is not one code point.
        for (int offset = 1; offset < length; ++offset) {
            if ((byte_at(index + static_cast<std::size_t>(offset)) & 0xC0) != 0x80) {
                ++index;
                return utf8_invalid;
            }
        }
        std::uint32_t value = 0;
        switch (length) {
        case 1:
            value = lead;
            break;
        case 2:
            value = static_cast<std::uint32_t>(lead & 0x1Fu);
            break;
        case 3:
            value = static_cast<std::uint32_t>(lead & 0x0Fu);
            break;
        default:
            value = static_cast<std::uint32_t>(lead & 0x07u);
            break;
        }
        for (int offset = 1; offset < length; ++offset) {
            value = (value << 6) | static_cast<std::uint32_t>(byte_at(index + static_cast<std::size_t>(offset)) & 0x3Fu);
        }
        // Overlong encodings and surrogates are not code points; answer the invalid marker.
        const bool overlong = (length == 2 && value < 0x80u) || (length == 3 && value < 0x800u)
            || (length == 4 && value < 0x10000u);
        const bool surrogate = value >= 0xD800u && value <= 0xDFFFu;
        if (overlong || surrogate || value > 0x10FFFFu) {
            ++index;
            return utf8_invalid;
        }
        index += static_cast<std::size_t>(length);
        return static_cast<int>(value);
    }

    /// @brief The next code-point boundary after @p position.
    /// @param text The text.
    /// @param position A byte offset inside (or at the end of) @p text.
    /// @return The offset just past the sequence that starts at @p position; the end of the text for a
    ///         position at or past it. A malformed lead byte advances one byte.
    /// @note noexcept and constexpr.
    constexpr std::size_t utf8_next_boundary(std::string_view text, std::size_t position) noexcept {
        if (position >= text.size()) {
            return text.size();
        }
        const int length = utf8_sequence_length(static_cast<unsigned char>(text[position]));
        const std::size_t step = length == 0 ? 1 : static_cast<std::size_t>(length);
        const std::size_t next = position + step;
        return next < text.size() ? next : text.size();
    }

    /// @brief The previous code-point boundary before @p position.
    /// @param text The text.
    /// @param position A byte offset inside @p text.
    /// @return The start of the sequence that ends at or before @p position; 0 at the start.
    /// @note It walks back over continuation bytes, so it is the exact inverse of utf8_next_boundary()
    ///       for well-formed text.
    /// @note noexcept and constexpr.
    constexpr std::size_t utf8_previous_boundary(std::string_view text, std::size_t position) noexcept {
        if (position == 0) {
            return 0;
        }
        std::size_t previous = position < text.size() ? position : text.size();
        if (previous > 0) {
            --previous;
        }
        while (previous > 0 && (static_cast<unsigned char>(text[previous]) & 0xC0) == 0x80) {
            --previous;
        }
        return previous;
    }

    /// @brief The number of code points in @p text.
    /// @param text The text.
    /// @return The count; every malformed byte counts as one (the replacement box it draws).
    /// @note noexcept and constexpr.
    constexpr std::size_t utf8_codepoint_count(std::string_view text) noexcept {
        std::size_t count = 0;
        for (std::size_t index = 0; index < text.size(); index = utf8_next_boundary(text, index)) {
            ++count;
        }
        return count;
    }

    /// @brief The number of code points before byte offset @p byte_position.
    /// @param text The text.
    /// @param byte_position The byte offset.
    /// @return The code-point index of that offset (a byte offset inside a sequence rounds down to the
    ///         sequence's own index).
    /// @note noexcept and constexpr.
    constexpr std::size_t utf8_codepoint_index(std::string_view text, std::size_t byte_position) noexcept {
        std::size_t count = 0;
        for (std::size_t index = 0;
             index < byte_position && index < text.size();
             index = utf8_next_boundary(text, index)) {
            ++count;
        }
        return count;
    }
}

#endif
