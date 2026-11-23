#ifndef HWSHQTB__AUDIO__DETAIL__SDL_AUDIO_BACKEND_H
#define HWSHQTB__AUDIO__DETAIL__SDL_AUDIO_BACKEND_H

/// @file detail/sdl_audio_backend.h
/// @brief Internal: the SDL3 implementation behind audio_device_t / audio_output_t.

#include <cstdint>

#include <SDL3/SDL.h>

#include "hwshqtb/audio/audio_format_t.h"

namespace hwshqtb::audio::detail {
    /// @brief Convert the layer's format to SDL's spec.
    /// @param format A valid layout.
    /// @return The matching SDL_AudioSpec.
    SDL_AudioSpec to_sdl_spec(const audio_format_t& format) noexcept;

    /// @brief Convert an SDL format word to the layer's enum.
    /// @param format SDL's SDL_AudioFormat.
    /// @param out Receives the layer's sample format.
    /// @return false when SDL's format has no counterpart here (an exotic layout).
    bool from_sdl_format(SDL_AudioFormat format, sample_format_enum& out) noexcept;

    /// @brief Initialise SDL's audio subsystem once, lazily.
    /// @return true when the subsystem is usable.
    /// @note Never throws: a machine without any audio driver is a supported state, and the callers turn
    ///       a false answer into a NULL device / NULL output.
    bool ensure_audio_ready() noexcept;

    /// @brief Open the backend stream for @p device.
    /// @param device_index SDL's device index.
    /// @param format The layout to open.
    /// @return The stream, or nullptr (SDL_GetError() explains).
    SDL_AudioStream* open_stream(int device_index, const audio_format_t& format) noexcept;
}

#endif
