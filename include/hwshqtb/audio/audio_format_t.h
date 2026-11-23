#ifndef HWSHQTB__AUDIO__AUDIO_FORMAT_T_H
#define HWSHQTB__AUDIO__AUDIO_FORMAT_T_H

/// @file audio_format_t.h
/// @brief audio_format_t: the description of a stream's sample layout (QAudioFormat's subset).

#include <cstdint>

#include "hwshqtb/audio/export.h"

namespace hwshqtb::audio {
    /// @brief How one sample is stored (QAudioFormat::SampleFormat).
    /// @note The numeric values are the layer's own, not SDL's: the engine converts at the seam, so a
    ///       caller never depends on which backend is installed.
    enum struct sample_format_enum : std::uint8_t {
        /// @brief Unsigned 8-bit, silence at 128.
        UINT8 = 0,
        /// @brief Signed 16-bit little-endian, silence at 0 (the common case).
        INT16 = 1,
        /// @brief Signed 32-bit little-endian, silence at 0.
        INT32 = 2,
        /// @brief 32-bit IEEE float in -1.0 .. 1.0.
        FLOAT = 3,
    };

    /// @brief The layout of one audio stream: sample rate, channel count and sample format.
    ///
    /// @par Value type
    /// Everything is constexpr and noexcept, and two formats compare equal when all three members are
    /// equal (QAudioFormat's operator==). A format with any member at zero/null is INVALID and the
    /// engine refuses to open a stream for it.
    ///
    /// @par Frames versus samples
    /// One FRAME is one sample of every channel, so bytes_per_frame() is bytes_per_sample() times the
    /// channel count. Durations are computed in frames, which is why a mono and a stereo stream of the
    /// same length have the same duration but different byte counts.
    ///
    /// @par Out of scope
    /// Qt also carries a ChannelConfig (the POSITION of each channel: front-left, LFE, ...) and
    /// channelOffset(). This layer does not: the backend counts channels and has no notion of where they
    /// sit, so a config here would have no consumer.
    class audio_format_t {
    public:
        /// @brief The number of sample frames per second.
        constexpr int sample_rate() const noexcept {
            return _sample_rate;
        }

        /// @brief Set the sample rate in Hz (44100, 48000, ...).
        constexpr void set_sample_rate(int rate) noexcept {
            _sample_rate = rate;
        }

        /// @brief The number of channels (1 mono, 2 stereo, ...).
        constexpr int channel_count() const noexcept {
            return _channel_count;
        }

        /// @brief Set the channel count.
        constexpr void set_channel_count(int count) noexcept {
            _channel_count = count;
        }

        /// @brief How one sample is stored.
        constexpr sample_format_enum sample_format() const noexcept {
            return _sample_format;
        }

        /// @brief Set the sample format.
        constexpr void set_sample_format(sample_format_enum format) noexcept {
            _sample_format = format;
        }

        /// @brief Whether all three members are set (QAudioFormat::isValid()).
        /// @note A default-constructed format is invalid, which is what makes an "empty" format testable
        ///       without a separate has_value flag.
        constexpr bool is_valid() const noexcept {
            return _sample_rate > 0 && _channel_count > 0;
        }

        /// @brief The bytes one sample of one channel occupies.
        constexpr int bytes_per_sample() const noexcept {
            switch (_sample_format) {
            case sample_format_enum::UINT8:
                return 1;
            case sample_format_enum::INT16:
                return 2;
            case sample_format_enum::INT32:
            case sample_format_enum::FLOAT:
                return 4;
            }
            return 0;
        }

        /// @brief The bytes one frame (one sample of every channel) occupies.
        constexpr int bytes_per_frame() const noexcept {
            return bytes_per_sample() * _channel_count;
        }

        /// @brief The frames that fit in @p bytes.
        /// @param bytes A byte count; a non-positive value answers 0.
        constexpr std::int64_t frames_for_bytes(std::int64_t bytes) const noexcept {
            const std::int64_t per_frame = bytes_per_frame();
            return (per_frame > 0 && bytes > 0) ? bytes / per_frame : 0;
        }

        /// @brief The bytes @p frames occupy.
        /// @param frames A frame count; a non-positive value answers 0.
        constexpr std::int64_t bytes_for_frames(std::int64_t frames) const noexcept {
            return frames > 0 ? frames * bytes_per_frame() : 0;
        }

        /// @brief The microseconds @p bytes play for.
        /// @param bytes A byte count.
        /// @note Integer arithmetic, like Qt's: the rate of rounding error is bounded by one microsecond
        ///       per call, which is far below anything a clock or a listener can observe.
        constexpr std::int64_t duration_for_bytes(std::int64_t bytes) const noexcept {
            return duration_for_frames(frames_for_bytes(bytes));
        }

        /// @brief The bytes that play for @p microseconds.
        /// @param microseconds A duration.
        constexpr std::int64_t bytes_for_duration(std::int64_t microseconds) const noexcept {
            return bytes_for_frames(frames_for_duration(microseconds));
        }

        /// @brief The microseconds @p frames play for.
        /// @param frames A frame count.
        constexpr std::int64_t duration_for_frames(std::int64_t frames) const noexcept {
            return (_sample_rate > 0 && frames > 0) ? frames * 1'000'000 / _sample_rate : 0;
        }

        /// @brief The frames @p microseconds play for.
        /// @param microseconds A duration.
        constexpr std::int64_t frames_for_duration(std::int64_t microseconds) const noexcept {
            return (_sample_rate > 0 && microseconds > 0) ? microseconds * _sample_rate / 1'000'000 : 0;
        }

        /// @brief Whether two formats describe the same stream.
        friend constexpr bool operator==(const audio_format_t& left, const audio_format_t& right) noexcept {
            return left._sample_rate == right._sample_rate && left._channel_count == right._channel_count
                && left._sample_format == right._sample_format;
        }

        /// @brief Whether two formats describe different streams.
        friend constexpr bool operator!=(const audio_format_t& left, const audio_format_t& right) noexcept {
            return !(left == right);
        }

    private:
        /// @brief Sample frames per second; 0 means unset.
        int _sample_rate = 0;
        /// @brief The channel count; 0 means unset.
        int _channel_count = 0;
        /// @brief How one sample is stored.
        sample_format_enum _sample_format = sample_format_enum::INT16;
    };
}

#endif
