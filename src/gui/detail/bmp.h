#ifndef HWSHQTB__GUI__DETAIL__BMP_H
#define HWSHQTB__GUI__DETAIL__BMP_H

/// @file detail/bmp.h
/// @brief Internal: the one BMP writer both platform windows share.

#include <string_view>

#include <SDL3/SDL.h>

namespace hwshqtb::gui::detail {
    /// @brief Normalise @p raw to 24-bit BGR and write it to @p path as a BMP, taking ownership of it.
    /// @param raw The surface read back from a renderer (may be any format); it is destroyed here,
    ///        whether the write succeeds or not.
    /// @param path The output file path.
    /// @param api The calling entry point's name, used as the error message's prefix.
    /// @throws std::runtime_error when the surface cannot be converted or the file cannot be written
    ///         (the message carries SDL_GetError()).
    /// @note The 24-bit BGR normalisation is what makes a capture byte-identical whichever backend
    ///       produced it: a window back buffer reads back as 32-bit ARGB, a software renderer as
    ///       24-bit RGB, and SDL_SaveBMP writes the surface's own format.
    /// @note Shared by every platform_window_t implementation, so all of them write the same file.
    void write_bmp(SDL_Surface* raw, std::string_view path, std::string_view api);

    /// @brief Load @p path as an ARGB8888 surface (the reader next to write_bmp()).
    /// @param path The input file path.
    /// @return A new surface the caller owns, or nullptr when the file is missing or not a BMP SDL can
    ///         decode (the message would be SDL_GetError()).
    /// @note The result is converted to SDL_PIXELFORMAT_ARGB8888, the one format the paint engine blits,
    ///       so a caller never has to know which depths or palettes the file used.
    SDL_Surface* load_bmp(std::string_view path);
}

#endif
