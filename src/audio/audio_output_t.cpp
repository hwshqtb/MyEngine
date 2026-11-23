#include "hwshqtb/audio/audio_output_t.h"

#include <algorithm>
#include <cstring>

#include "detail/sdl_audio_backend.h"

namespace hwshqtb::audio {
    /// @brief The SDL stream behind an audio_output_t.
    struct audio_output_t::impl {
        /// @brief The backend stream; nullptr when SDL refused to open it.
        SDL_AudioStream* stream = nullptr;
        /// @brief The backend's own queue capacity in bytes (SDL's default when not set).
        int capacity = 0;
    };

    audio_output_t::audio_output_t(const audio_device_t& device, const audio_format_t& format) {
        // The device answers with the layout it can actually open; a NULL device answers the request
        // unchanged, and then nothing opens — a NULL output, which is a supported state.
        _format = device.nearest_format(format);
        if (device.is_null() || !_format.is_valid()) {
            return;
        }
        SDL_AudioStream* const stream = detail::open_stream(device.index(), _format);
        if (stream == nullptr) {
            return; // no driver, or the backend refused: stay a NULL output
        }
        auto opened = std::make_unique<impl>();
        opened->stream = stream;
        opened->capacity = SDL_GetAudioStreamQueued(stream);
        _impl = std::move(opened);
        // The default capacity is one second of audio: long enough to survive a slow frame loop, short
        // enough that a stop() is audible immediately.
        set_buffer_size(_format.bytes_for_duration(1'000'000));
    }

    audio_output_t::~audio_output_t() {
        if (_impl != nullptr && _impl->stream != nullptr) {
            SDL_DestroyAudioStream(_impl->stream);
        }
    }

    audio_output_t::audio_output_t(audio_output_t&& other) noexcept:
        _impl(std::move(other._impl)),
        _format(other._format),
        _buffer_size(other._buffer_size),
        _written(other._written),
        _volume(other._volume),
        _state(other._state) { }

    audio_output_t& audio_output_t::operator=(audio_output_t&& other) noexcept {
        if (this != &other) {
            if (_impl != nullptr && _impl->stream != nullptr) {
                SDL_DestroyAudioStream(_impl->stream);
            }
            _impl = std::move(other._impl);
            _format = other._format;
            _buffer_size = other._buffer_size;
            _written = other._written;
            _volume = other._volume;
            _state = other._state;
        }
        return *this;
    }

    void audio_output_t::start() {
        if (_impl == nullptr) {
            return;
        }
        _written = 0;
        if (SDL_ResumeAudioStreamDevice(_impl->stream)) {
            _state = audio_state_enum::ACTIVE;
        }
    }

    void audio_output_t::stop() {
        if (_impl == nullptr) {
            return;
        }
        (void)SDL_PauseAudioStreamDevice(_impl->stream);
        (void)SDL_ClearAudioStream(_impl->stream);
        _written = 0;
        _state = audio_state_enum::STOPPED;
    }

    void audio_output_t::suspend() {
        if (_impl == nullptr || _state == audio_state_enum::STOPPED) {
            return;
        }
        (void)SDL_PauseAudioStreamDevice(_impl->stream);
        _state = audio_state_enum::SUSPENDED;
    }

    void audio_output_t::resume() {
        if (_impl == nullptr || _state != audio_state_enum::SUSPENDED) {
            return;
        }
        if (SDL_ResumeAudioStreamDevice(_impl->stream)) {
            _state = audio_state_enum::ACTIVE;
        }
    }

    audio_state_enum audio_output_t::state() const noexcept {
        if (_impl == nullptr) {
            return audio_state_enum::STOPPED;
        }
        // Started and consuming is ACTIVE; started with an empty queue is IDLE (an underrun). The
        // distinction is what lets a caller decide whether to push more data.
        if (_state == audio_state_enum::ACTIVE && bytes_queued() == 0 && _written > 0) {
            return audio_state_enum::IDLE;
        }
        return _state;
    }

    std::int64_t audio_output_t::write(const void* bytes, std::int64_t count) {
        if (_impl == nullptr) {
            return -1; // QAudioSink::write() answers -1 on a null sink
        }
        if (bytes == nullptr || count <= 0) {
            return 0;
        }
        // Never push past the capacity, so bytes_free() means something to the caller's loop.
        const std::int64_t room = bytes_free();
        const std::int64_t wanted = std::min(count, room);
        if (wanted <= 0) {
            return 0;
        }
        const int accepted = wanted > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(wanted);
        if (!SDL_PutAudioStreamData(_impl->stream, bytes, accepted)) {
            return 0;
        }
        _written += accepted;
        if (_state == audio_state_enum::STOPPED) {
            // SDL starts consuming once data arrives; mirror that in the state so a caller that never
            // called start() still sees ACTIVE (QAudioSink behaves the same on the first write).
            _state = audio_state_enum::ACTIVE;
        }
        return accepted;
    }

    std::int64_t audio_output_t::write_some(const audio_clip_t& clip, std::int64_t& offset) {
        if (_impl == nullptr) {
            return -1;
        }
        const std::int64_t total = static_cast<std::int64_t>(clip.samples().size());
        if (offset >= total) {
            return 0; // exhausted: the caller decides whether to rewind
        }
        const std::int64_t accepted =
            write(clip.samples().data() + offset, total - offset);
        if (accepted > 0) {
            offset += accepted;
        }
        return accepted;
    }

    std::int64_t audio_output_t::bytes_free() const noexcept {
        if (_impl == nullptr) {
            return 0;
        }
        return std::max<std::int64_t>(0, _buffer_size - bytes_queued());
    }

    void audio_output_t::set_buffer_size(std::int64_t bytes) {
        const std::int64_t one_frame = _format.bytes_per_frame();
        _buffer_size = std::max(bytes, one_frame > 0 ? one_frame : 1);
        if (_impl != nullptr) {
            // SDL's capacity is its own business; we only enforce ours on the way in, which is what
            // keeps bytes_free() honest without depending on a backend-specific setter.
            (void)SDL_SetAudioStreamGain(_impl->stream, static_cast<float>(_volume));
        }
    }

    std::int64_t audio_output_t::bytes_queued() const noexcept {
        if (_impl == nullptr) {
            return 0;
        }
        const int queued = SDL_GetAudioStreamQueued(_impl->stream);
        return queued > 0 ? queued : 0;
    }

    std::int64_t audio_output_t::processed_us() const noexcept {
        if (_impl == nullptr) {
            return 0;
        }
        // Playback, not the clock: what was written minus what is still queued.
        return _format.duration_for_bytes(std::max<std::int64_t>(0, _written - bytes_queued()));
    }

    void audio_output_t::set_volume(double volume) {
        _volume = std::clamp(volume, 0.0, 1.0);
        apply_volume();
    }

    void audio_output_t::apply_volume() {
        if (_impl == nullptr) {
            return;
        }
        (void)SDL_SetAudioStreamGain(_impl->stream, static_cast<float>(_volume));
    }
}
