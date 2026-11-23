#ifndef HWSHQTB__AUDIO__AUDIO_DECODER_T_H
#define HWSHQTB__AUDIO__AUDIO_DECODER_T_H

/// @file audio_decoder_t.h
/// @brief The sound-file decoding seam: what load_audio() asks, and how the defaults plug in.

#include <cstdint>
#include <string_view>

#include "hwshqtb/audio/audio_clip_t.h"
#include "hwshqtb/audio/export.h"

namespace hwshqtb::audio {
    /// @brief A sound-file format the layer recognises.
    /// @note Identified by the file's MAGIC BYTES, never by its extension: an MP3 named .wav still
    ///       decodes as an MP3, and a file with no known magic is reported as UNKNOWN instead of being
    ///       handed to a decoder that would fail with a vague message.
    enum struct audio_file_format_enum : std::uint8_t {
        /// @brief No known magic: nothing can decode it.
        UNKNOWN = 0,
        /// @brief A RIFF/WAVE file ("RIFF....WAVE").
        WAV = 1,
        /// @brief An MPEG audio layer III stream (an ID3v2 tag, or a bare frame sync).
        MP3 = 2,
    };

    /// @brief The format @p header begins with.
    /// @param header The first bytes of a file (12 is enough for every signature here).
    /// @return The format, or audio_file_format_enum::UNKNOWN when no signature matches.
    /// @note noexcept: pure byte comparison, no allocation.
    HWSHQTB_AUDIO_EXPORT audio_file_format_enum audio_file_format_of(std::string_view header) noexcept;

    /// @brief One backend that turns an encoded sound file into a clip (the audio analogue of
    ///        image_decoder_t).
    ///
    /// The audio layer never decodes a format itself: it sniffs the magic bytes, picks a format and asks
    /// an installed decoder. WAV and MP3 therefore have separate implementations — WAV through SDL's own
    /// loader, MP3 through the vendored minimp3 — and neither is baked into the interface.
    class HWSHQTB_AUDIO_EXPORT audio_decoder_t {
    public:
        audio_decoder_t() = default;
        virtual ~audio_decoder_t();
        audio_decoder_t(const audio_decoder_t&) = delete;
        audio_decoder_t& operator=(const audio_decoder_t&) = delete;
        audio_decoder_t(audio_decoder_t&&) = delete;
        audio_decoder_t& operator=(audio_decoder_t&&) = delete;

        /// @brief Whether this backend can decode @p format.
        /// @param format The format sniffed from the file's magic bytes; never UNKNOWN.
        /// @note noexcept.
        virtual bool supports(audio_file_format_enum format) const noexcept = 0;

        /// @brief Decode @p path as @p format.
        /// @param path The input file path.
        /// @param format The format detected from the magic bytes.
        /// @return The fully decoded clip, in one of the sample_format_enum layouts.
        /// @throws std::runtime_error when the file cannot be read or decoded; the message names the
        ///         format and carries the backend's own error text.
        virtual audio_clip_t decode(std::string_view path, audio_file_format_enum format) = 0;

        /// @brief A diagnostic name ("sdl", "minimp3", ...).
        /// @note noexcept.
        virtual std::string_view name() const noexcept = 0;
    };

    /// @brief Install a decoder that answers BEFORE the built-in ones (nullptr removes it).
    /// @param decoder The decoder to try first; not owned, and it must outlive its use.
    /// @note The built-in defaults stay installed as fallbacks, so installing one never makes the
    ///       documented formats undecodable — it only adds a preference.
    HWSHQTB_AUDIO_EXPORT void set_audio_decoder(audio_decoder_t* decoder) noexcept;

    /// @brief The decoder that will be tried first (nullptr when none was installed).
    /// @note The built-in defaults (SDL for WAV, minimp3 for MP3) are always tried after it.
    HWSHQTB_AUDIO_EXPORT audio_decoder_t* audio_decoder() noexcept;

    /// @brief Load @p path as a clip: sniff the magic bytes, then let a decoder decode it.
    /// @param path The input file path (WAV and MP3 are recognised today).
    /// @return The decoded clip.
    /// @throws std::runtime_error when the file cannot be read, its format is UNKNOWN, or no decoder
    ///         handles it. The message names the format when one was recognised.
    HWSHQTB_AUDIO_EXPORT audio_clip_t load_audio(std::string_view path);
}

#endif
