#include "sdl_audio_backend.h"

#include <mutex>

namespace hwshqtb::audio::detail {
    SDL_AudioSpec to_sdl_spec(const audio_format_t& format) noexcept {
        SDL_AudioSpec spec{};
        spec.freq = format.sample_rate();
        spec.channels = format.channel_count();
        switch (format.sample_format()) {
        case sample_format_enum::UINT8:
            spec.format = SDL_AUDIO_U8;
            break;
        case sample_format_enum::INT16:
            spec.format = SDL_AUDIO_S16LE;
            break;
        case sample_format_enum::INT32:
            spec.format = SDL_AUDIO_S32LE;
            break;
        case sample_format_enum::FLOAT:
            spec.format = SDL_AUDIO_F32LE;
            break;
        }
        return spec;
    }

    bool from_sdl_format(SDL_AudioFormat format, sample_format_enum& out) noexcept {
        switch (format) {
        case SDL_AUDIO_U8:
            out = sample_format_enum::UINT8;
            return true;
        case SDL_AUDIO_S16LE:
        case SDL_AUDIO_S16BE:
            out = sample_format_enum::INT16;
            return true;
        case SDL_AUDIO_S32LE:
        case SDL_AUDIO_S32BE:
            out = sample_format_enum::INT32;
            return true;
        case SDL_AUDIO_F32LE:
        case SDL_AUDIO_F32BE:
            out = sample_format_enum::FLOAT;
            return true;
        default:
            return false;
        }
    }

    bool ensure_audio_ready() noexcept {
        // One-time initialisation, guarded so two threads opening a stream at once cannot both init.
        static std::once_flag once;
        static bool ready = false;
        std::call_once(once, [] {
            // SDL_INIT_AUDIO failing is normal on a headless runner: no driver, no device, no error.
            ready = SDL_InitSubSystem(SDL_INIT_AUDIO);
        });
        return ready;
    }

    SDL_AudioStream* open_stream(int device_index, const audio_format_t& format) noexcept {
        if (!ensure_audio_ready() || !format.is_valid()) {
            return nullptr;
        }
        // SDL identifies a device by its own id, which is what audio_device_t::index() carries; a
        // negative index means the system default.
        const SDL_AudioDeviceID device = device_index >= 0
            ? static_cast<SDL_AudioDeviceID>(device_index)
            : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
        const SDL_AudioSpec spec = to_sdl_spec(format);
        // A NULL callback is the point: SDL's audio thread then only touches its own buffer and never
        // calls back into this layer, which is what makes audio_output_t lock-free (see its header).
        return SDL_OpenAudioDeviceStream(device, &spec, nullptr, nullptr);
    }
}
