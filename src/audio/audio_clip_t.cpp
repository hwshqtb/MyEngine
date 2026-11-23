#include "hwshqtb/audio/audio_clip_t.h"

namespace hwshqtb::audio {
    audio_clip_t::audio_clip_t(audio_format_t format, std::vector<std::byte> samples):
        _format(format),
        _samples(std::move(samples)) { }

    std::int64_t audio_clip_t::frame_count() const noexcept {
        return _format.frames_for_bytes(static_cast<std::int64_t>(_samples.size()));
    }

    std::int64_t audio_clip_t::duration_us() const noexcept {
        return _format.duration_for_frames(frame_count());
    }
}
