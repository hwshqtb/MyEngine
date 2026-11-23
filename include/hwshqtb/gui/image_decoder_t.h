#ifndef HWSHQTB__GUI__IMAGE_DECODER_T_H
#define HWSHQTB__GUI__IMAGE_DECODER_T_H

/// @file image_decoder_t.h
/// @brief The image-decoding seam: what load_image() asks, and how the default (SDL) plugs in.

#include <cstdint>
#include <string_view>

#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/image_t.h"

namespace hwshqtb::gui {
    /// @brief A raster format the layer recognises.
    /// @note Identified by the file's MAGIC BYTES, never by its extension: a PNG named .bmp still decodes
    ///       as a PNG, and a file with no known magic is reported as UNKNOWN instead of being handed to a
    ///       decoder that would fail with a vague message.
    enum struct image_format_enum : std::uint8_t {
        /// @brief No known magic: nothing can decode it.
        UNKNOWN = 0,
        /// @brief A Windows bitmap ("BM").
        BMP = 1,
        /// @brief A PNG (the 8-byte 0x89 'P' 'N' 'G' signature).
        PNG = 2,
        /// @brief A JPEG (0xFF 0xD8 0xFF).
        JPEG = 3,
    };

    /// @brief The format @p header begins with.
    /// @param header The first bytes of a file (8 is enough; fewer is allowed).
    /// @return The format, or image_format_enum::UNKNOWN when no signature matches.
    /// @note noexcept: pure byte comparison, no allocation.
    HWSHQTB_GUI_EXPORT image_format_enum image_format_of(std::string_view header) noexcept;

    /// @brief One backend that turns an encoded file into an image (the image analogue of font_engine_t).
    ///
    /// The gui layer never decodes a format itself: it sniffs the magic bytes, picks a format and asks the
    /// installed decoder. That keeps the layer free of any one library's format support — the default
    /// happens to be SDL, but it is a DEFAULT, not a dependency of the interface.
    class HWSHQTB_GUI_EXPORT image_decoder_t {
    public:
        image_decoder_t() = default;
        virtual ~image_decoder_t();
        image_decoder_t(const image_decoder_t&) = delete;
        image_decoder_t& operator=(const image_decoder_t&) = delete;
        image_decoder_t(image_decoder_t&&) = delete;
        image_decoder_t& operator=(image_decoder_t&&) = delete;

        /// @brief Whether this backend can decode @p format.
        /// @param format The format sniffed from the file's magic bytes; never UNKNOWN.
        /// @return true when decode() accepts it.
        /// @note noexcept.
        virtual bool supports(image_format_enum format) const noexcept = 0;

        /// @brief Decode @p path as @p format.
        /// @param path The input file path.
        /// @param format The format detected from the magic bytes.
        /// @return The image, in ARGB8888 whatever the file's own depth was.
        /// @throws std::runtime_error when the file cannot be read or decoded; the message names the
        ///         format and carries the backend's own error text.
        virtual image_t decode(std::string_view path, image_format_enum format) = 0;

        /// @brief A diagnostic name ("sdl", "builtin", ...).
        /// @note noexcept.
        virtual std::string_view name() const noexcept = 0;
    };

    /// @brief Install the process-wide decoder (nullptr restores the built-in default).
    /// @param decoder The decoder to use; not owned, and it must outlive its use.
    /// @note The default is the SDL backend, installed on first use.
    HWSHQTB_GUI_EXPORT void set_image_decoder(image_decoder_t* decoder) noexcept;

    /// @brief The decoder load_image() will use (never null once something has been loaded).
    HWSHQTB_GUI_EXPORT image_decoder_t* image_decoder() noexcept;

    /// @brief Load @p path as an image: sniff the magic bytes, then let the decoder decode it.
    /// @param path The input file path (BMP, PNG and JPEG are recognised today).
    /// @return The decoded image, in ARGB8888.
    /// @throws std::runtime_error when the file cannot be read, its format is UNKNOWN, or the decoder
    ///         refuses it. The message names the format when one was recognised.
    HWSHQTB_GUI_EXPORT image_t load_image(std::string_view path);
}

#endif
