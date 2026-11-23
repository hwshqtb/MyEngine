#ifndef HWSHQTB__AUDIO__AUDIO_CLIP_T_H
#define HWSHQTB__AUDIO__AUDIO_CLIP_T_H

/// @file audio_clip_t.h
/// @brief audio_clip_t: one decoded sound, ready to be pushed to an output.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "hwshqtb/audio/audio_format_t.h"
#include "hwshqtb/audio/export.h"

namespace hwshqtb::audio {
    /// @brief A block of decoded samples together with the format they are in.
    ///
    /// @par Fully decoded, fully owned
    /// A clip holds the WHOLE sound in memory in the engine's own layout. That is what makes looping a
    /// music track trivial (rewind the read cursor) and what keeps the push loop free of decoding work:
    /// decode once, push forever.
    ///
    /// @par Why a byte vector and not a sample vector
    /// The samples are handed to the backend as raw bytes, so bytes are the honest representation: a
    /// std::vector<std::int16_t> would force a reinterpret_cast at the seam and would be wrong for FLOAT
    /// or UINT8 clips.
    class HWSHQTB_AUDIO_EXPORT audio_clip_t {
    public:
        /// @brief An empty clip (no samples, invalid format).
        audio_clip_t() = default;

        /// @brief Build a clip from samples that are already in @p format.
        /// @param format The layout of @p samples.
        /// @param samples The raw sample bytes; moved in.
        audio_clip_t(audio_format_t format, std::vector<std::byte> samples);

        /// @brief The format the samples are in.
        constexpr const audio_format_t& format() const noexcept {
            return _format;
        }

        /// @brief The raw sample bytes.
        constexpr const std::vector<std::byte>& samples() const noexcept {
            return _samples;
        }

        /// @brief The number of sample frames in the clip.
        std::int64_t frame_count() const noexcept;

        /// @brief How long the clip plays for, in microseconds.
        std::int64_t duration_us() const noexcept;

        /// @brief Whether the clip carries no samples.
        constexpr bool is_empty() const noexcept {
            return _samples.empty();
        }

    private:
        /// @brief The layout of the samples.
        audio_format_t _format{};
        /// @brief The raw sample bytes.
        std::vector<std::byte> _samples;
    };
}

#endif
