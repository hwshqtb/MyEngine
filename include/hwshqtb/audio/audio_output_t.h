#ifndef HWSHQTB__AUDIO__AUDIO_OUTPUT_T_H
#define HWSHQTB__AUDIO__AUDIO_OUTPUT_T_H

/// @file audio_output_t.h
/// @brief audio_output_t: one push-mode output stream (QAudioSink's push half).

#include <cstdint>
#include <memory>

#include "hwshqtb/audio/audio_clip_t.h"
#include "hwshqtb/audio/audio_device_t.h"
#include "hwshqtb/audio/audio_format_t.h"
#include "hwshqtb/audio/export.h"

namespace hwshqtb::audio {
    /// @brief The lifecycle state of an audio_output_t (QAudio::State's subset).
    enum struct audio_state_enum : std::uint8_t {
        /// @brief Opened but not started, or stopped: nothing is being played.
        STOPPED = 0,
        /// @brief Started and consuming: the backend is draining the queue.
        ACTIVE = 1,
        /// @brief Started but idle: the queue ran dry (an underrun).
        IDLE = 2,
        /// @brief Suspended: the device is paused, the queue is kept.
        SUSPENDED = 3,
    };

    /// @brief One output stream, PUSH mode: the application writes sample bytes.
    ///
    /// @par Push only, deliberately
    /// Qt's QAudioSink also has a pull mode (it hands you a QIODevice to write into on demand). That mode
    /// needs a get-callback registered with the backend and a second object lifetime; push covers every
    /// use this layer has (play a decoded clip, loop a track, fire a one-shot), so pull is out of scope.
    ///
    /// @par Threading: no locks, and why
    /// The stream is opened with a NULL callback, so the backend's audio thread only ever touches its own
    /// internal buffer — it never calls back into this object. Every member here therefore runs on the
    /// thread that created the object, and no synchronisation is needed. (A pull-mode sink would break
    /// that argument immediately, which is a second reason it is absent.)
    ///
    /// @par One-shot sounds
    /// Two outputs opened on the same device mix, so a short effect played on its own output overlaps a
    /// music track playing on another. That is the intended way to fire a click while a track loops.
    class HWSHQTB_AUDIO_EXPORT audio_output_t {
    public:
        /// @brief Open a push-mode output stream.
        /// @param device The output device; the NULL device (or any invalid format) makes this a NULL
        ///        output, which accepts every call and plays nothing.
        /// @param format The wanted layout. The device's nearest_format() is used when it differs, and
        ///        format() reports what was actually opened.
        /// @note Never throws: a backend that refuses to open produces a NULL output, so a caller can run
        ///       identically on a machine with no sound card. Query is_null() to tell the difference.
        audio_output_t(const audio_device_t& device, const audio_format_t& format);

        /// @brief Close the stream (the backend's device goes away).
        ~audio_output_t();

        audio_output_t(const audio_output_t&) = delete;
        audio_output_t& operator=(const audio_output_t&) = delete;
        audio_output_t(audio_output_t&& other) noexcept;
        audio_output_t& operator=(audio_output_t&& other) noexcept;

        /// @brief Whether the stream failed to open (no device, invalid format, backend refusal).
        /// @note noexcept.
        bool is_null() const noexcept {
            return _impl == nullptr;
        }

        /// @brief The layout the stream was actually opened with.
        /// @note noexcept; invalid for a NULL output.
        const audio_format_t& format() const noexcept {
            return _format;
        }

        /// @brief Start (or resume) playback.
        /// @note A NULL output ignores it.
        void start();

        /// @brief Stop playback and drop everything queued.
        /// @note A NULL output ignores it.
        void stop();

        /// @brief Pause playback, KEEPING the queue (QAudioSink::suspend()).
        /// @note A NULL output ignores it.
        void suspend();

        /// @brief Continue a suspended stream, keeping the queue (QAudioSink::resume()).
        /// @note A NULL output ignores it.
        void resume();

        /// @brief The lifecycle state.
        /// @note noexcept; STOPPED for a NULL output.
        audio_state_enum state() const noexcept;

        /// @brief Push @p bytes of sample data.
        /// @param bytes The sample bytes, in format()'s layout.
        /// @param count How many bytes to push; a non-positive count pushes nothing.
        /// @return The number of bytes ACCEPTED, which may be less than @p bytes when the buffer is
        ///         nearly full; 0 when it is full; -1 for a NULL output (QAudioSink::write()).
        /// @note A partial write is normal: call again with the rest.
        std::int64_t write(const void* bytes, std::int64_t count);

        /// @brief Push as much of @p clip as fits right now, without blocking.
        /// @param clip The clip to push from.
        /// @param offset The byte offset inside the clip to start at; advanced by the bytes accepted.
        /// @return The bytes accepted (0 when the buffer is full or the clip is exhausted).
        /// @note This is the loop-friendly form: a caller that plays a track repeatedly keeps an offset
        ///       and calls this until it answers 0, then waits for the next frame.
        std::int64_t write_some(const audio_clip_t& clip, std::int64_t& offset);

        /// @brief How many bytes can be written before the buffer is full.
        /// @note noexcept; 0 for a NULL output.
        std::int64_t bytes_free() const noexcept;

        /// @brief The queue capacity in bytes.
        /// @note noexcept; 0 for a NULL output.
        std::int64_t buffer_size() const noexcept {
            return _buffer_size;
        }

        /// @brief Set the queue capacity, in bytes.
        /// @param bytes The wanted capacity; clamped to at least one frame and applied by the backend.
        /// @note A larger buffer survives a slow frame loop without an underrun at the cost of latency.
        void set_buffer_size(std::int64_t bytes);

        /// @brief The bytes queued but not yet played.
        /// @note noexcept.
        std::int64_t bytes_queued() const noexcept;

        /// @brief How much audio has been played since start(), in microseconds.
        /// @note Computed from the bytes written minus the bytes still queued, so it measures playback and
        ///       not the clock (QAudioSink::processedUSecs()).
        std::int64_t processed_us() const noexcept;

        /// @brief The output volume, 0.0 .. 1.0.
        /// @note noexcept.
        double volume() const noexcept {
            return _volume;
        }

        /// @brief Set the volume, clamped to 0.0 .. 1.0.
        /// @param volume The linear factor; the backend applies it to the samples it plays.
        void set_volume(double volume);

    private:
        struct impl;

        /// @brief Apply the volume to the backend.
        void apply_volume();

        /// @brief The backend stream; null for a NULL output.
        std::unique_ptr<impl> _impl;
        /// @brief The layout actually opened.
        audio_format_t _format{};
        /// @brief The queue capacity in bytes.
        std::int64_t _buffer_size = 0;
        /// @brief The bytes written since the last start().
        std::int64_t _written = 0;
        /// @brief The linear volume factor.
        double _volume = 1.0;
        /// @brief The lifecycle state.
        audio_state_enum _state = audio_state_enum::STOPPED;
    };
}

#endif
