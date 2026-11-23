#include "hwshqtb/audio/audio_device_t.h"

#include <utility>

#include "detail/sdl_audio_backend.h"

namespace hwshqtb::audio {
    audio_device_t::audio_device_t(int index, std::string id, std::string description,
                                   audio_format_t preferred, bool is_default):
        _null(false),
        _index(index),
        _id(std::move(id)),
        _description(std::move(description)),
        _preferred(preferred),
        _is_default(is_default) { }

    bool audio_device_t::is_format_supported(const audio_format_t& format) const noexcept {
        // A valid format is always supported: the backend converts or resamples what it must, so
        // accepting only one exact layout would reject perfectly playable requests.
        return !is_null() && format.is_valid();
    }

    audio_format_t audio_device_t::nearest_format(const audio_format_t& format) const {
        if (is_format_supported(format)) {
            return format;
        }
        // No device at all: answer the request unchanged rather than an empty format, so a caller's
        // arithmetic (buffer sizes, durations) keeps working on a machine without sound.
        return is_null() ? format : _preferred;
    }

    std::vector<audio_device_t> audio_outputs() {
        std::vector<audio_device_t> devices;
        if (!detail::ensure_audio_ready()) {
            return devices; // no audio driver: an empty list is the honest answer
        }
        // The default comes first, so a caller that takes devices.front() gets the expected device.
        audio_format_t preferred{};
        preferred.set_sample_rate(48'000);
        preferred.set_channel_count(2);
        preferred.set_sample_format(sample_format_enum::INT16);
        devices.push_back(audio_device_t{ -1, "default", "Default output", preferred, true });
        int count = 0;
        SDL_AudioDeviceID* const ids = SDL_GetAudioPlaybackDevices(&count);
        if (ids != nullptr) {
            for (int index = 0; index < count; ++index) {
                const char* const name = SDL_GetAudioDeviceName(ids[index]);
                SDL_AudioSpec spec{};
                audio_format_t format = preferred;
                if (SDL_GetAudioDeviceFormat(ids[index], &spec, nullptr)) {
                    sample_format_enum sample_format = sample_format_enum::INT16;
                    if (detail::from_sdl_format(spec.format, sample_format)) {
                        format.set_sample_rate(spec.freq);
                        format.set_channel_count(spec.channels);
                        format.set_sample_format(sample_format);
                    }
                }
                devices.push_back(audio_device_t{ static_cast<int>(ids[index]),
                                                  std::to_string(ids[index]),
                                                  name != nullptr ? name : "Output device", format,
                                                  false });
            }
            SDL_free(ids);
        }
        return devices;
    }

    audio_device_t default_audio_output() {
        std::vector<audio_device_t> devices = audio_outputs();
        if (devices.empty()) {
            return audio_device_t{}; // the NULL device
        }
        return std::move(devices.front());
    }
}
