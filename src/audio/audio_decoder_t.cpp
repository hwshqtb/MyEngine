#include "hwshqtb/audio/audio_decoder_t.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

// minimp3 is C, and its header is included by C++ here: the declarations carry extern "C" themselves.
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3_ex.h"

#include "detail/sdl_audio_backend.h"

namespace hwshqtb::audio {
    namespace {
        /// @brief The RIFF/WAVE signature: "RIFF" at 0, "WAVE" at 8.
        constexpr std::string_view riff_magic = "RIFF";
        constexpr std::string_view wave_magic = "WAVE";
        /// @brief The ID3v2 tag every tagged MP3 begins with.
        constexpr std::string_view id3_magic = "ID3";

        /// @brief The WAV half of the default decoder: SDL_LoadWAV, normalised to a layer format.
        class wav_decoder_t final: public audio_decoder_t {
        public:
            bool supports(audio_file_format_enum format) const noexcept override {
                return format == audio_file_format_enum::WAV;
            }

            audio_clip_t decode(std::string_view path, audio_file_format_enum) override {
                const std::string path_copy{ path };
                SDL_AudioSpec spec{};
                std::uint8_t* buffer = nullptr;
                std::uint32_t length = 0;
                if (!SDL_LoadWAV(path_copy.c_str(), &spec, &buffer, &length)) {
                    throw std::runtime_error(std::string("load_audio: cannot decode wav '") + path_copy
                                             + "': " + SDL_GetError());
                }
                audio_format_t format{};
                format.set_sample_rate(spec.freq);
                format.set_channel_count(spec.channels);
                sample_format_enum sample_format = sample_format_enum::INT16;
                if (!detail::from_sdl_format(spec.format, sample_format)) {
                    SDL_free(buffer);
                    throw std::runtime_error(std::string("load_audio: unsupported wav sample format in '")
                                             + path_copy + "'");
                }
                format.set_sample_format(sample_format);
                std::vector<std::byte> samples(length);
                std::memcpy(samples.data(), buffer, length);
                SDL_free(buffer);
                return audio_clip_t{ format, std::move(samples) };
            }

            std::string_view name() const noexcept override {
                return "sdl";
            }
        };

        /// @brief The MP3 half: the vendored minimp3 decoder.
        /// @note SDL3 has no MP3 support, so this is why the seam exists at all: two formats, two
        ///       backends, one interface.
        class mp3_decoder_t final: public audio_decoder_t {
        public:
            bool supports(audio_file_format_enum format) const noexcept override {
                return format == audio_file_format_enum::MP3;
            }

            audio_clip_t decode(std::string_view path, audio_file_format_enum) override {
                const std::string path_copy{ path };
                // mp3dec_ex_open() reads the whole file and indexes its frames, so the decoder can seek
                // and report the exact sample count up front.
                mp3dec_ex_t decoder{};
                if (mp3dec_ex_open(&decoder, path_copy.c_str(), MP3D_SEEK_TO_SAMPLE) != 0) {
                    throw std::runtime_error(std::string("load_audio: cannot decode mp3 '") + path_copy
                                             + "': not an MPEG audio stream");
                }
                const std::size_t total = decoder.samples;
                std::vector<std::int16_t> interleaved(total);
                const std::size_t read = mp3dec_ex_read(&decoder, interleaved.data(), total);
                const int rate = decoder.info.hz;
                const int channels = decoder.info.channels;
                mp3dec_ex_close(&decoder);
                interleaved.resize(read);
                audio_format_t format{};
                format.set_sample_rate(rate);
                format.set_channel_count(channels);
                format.set_sample_format(sample_format_enum::INT16); // minimp3 always decodes to s16
                std::vector<std::byte> samples(interleaved.size() * sizeof(std::int16_t));
                std::memcpy(samples.data(), interleaved.data(), samples.size());
                return audio_clip_t{ format, std::move(samples) };
            }

            std::string_view name() const noexcept override {
                return "minimp3";
            }
        };

        /// @brief The installed preference, or nullptr.
        audio_decoder_t*& installed_decoder() noexcept {
            static audio_decoder_t* installed = nullptr;
            return installed;
        }

        /// @brief The built-in fallbacks, created on first use.
        /// @return WAV (SDL) and MP3 (minimp3), in that order.
        std::vector<audio_decoder_t*>& default_decoders() {
            static wav_decoder_t wav;
            static mp3_decoder_t mp3;
            static std::vector<audio_decoder_t*> decoders{ &wav, &mp3 };
            return decoders;
        }

        /// @brief A name for @p format, for error messages.
        std::string_view format_name(audio_file_format_enum format) noexcept {
            switch (format) {
            case audio_file_format_enum::WAV:
                return "wav";
            case audio_file_format_enum::MP3:
                return "mp3";
            default:
                return "unknown";
            }
        }
    }

    audio_decoder_t::~audio_decoder_t() = default;

    audio_file_format_enum audio_file_format_of(std::string_view header) noexcept {
        // RIFF....WAVE: the type field sits at offset 8, so both halves have to match.
        if (header.size() >= 12 && header.substr(0, 4) == riff_magic && header.substr(8, 4) == wave_magic) {
            return audio_file_format_enum::WAV;
        }
        if (header.size() >= id3_magic.size() && header.substr(0, id3_magic.size()) == id3_magic) {
            return audio_file_format_enum::MP3;
        }
        // A bare MP3 with no tag starts at the frame sync: eleven set bits, 0xFF then 0xE0 set in the top
        // three bits of the second byte.
        if (header.size() >= 2 && static_cast<unsigned char>(header[0]) == 0xFF
            && (static_cast<unsigned char>(header[1]) & 0xE0) == 0xE0) {
            return audio_file_format_enum::MP3;
        }
        return audio_file_format_enum::UNKNOWN;
    }

    void set_audio_decoder(audio_decoder_t* decoder) noexcept {
        installed_decoder() = decoder;
    }

    audio_decoder_t* audio_decoder() noexcept {
        return installed_decoder();
    }

    audio_clip_t load_audio(std::string_view path) {
        const std::string path_copy{ path };
        // Sniff the magic bytes FIRST: an unknown file is reported as such, instead of being handed to a
        // decoder that can only answer "not a wav".
        std::string header;
        if (std::ifstream in{ path_copy, std::ios::binary }) {
            char bytes[12] = {};
            in.read(bytes, sizeof(bytes));
            header.assign(bytes, static_cast<std::size_t>(in.gcount()));
        } else {
            throw std::runtime_error(std::string("load_audio: cannot open '") + path_copy + "'");
        }
        const audio_file_format_enum format = audio_file_format_of(header);
        if (format == audio_file_format_enum::UNKNOWN) {
            throw std::runtime_error(std::string("load_audio: '") + path_copy
                                     + "' is not a WAV or MP3 (no known magic bytes)");
        }
        // The installed decoder gets the first word; the built-in defaults remain as fallbacks, so adding
        // one never makes a documented format undecodable.
        if (audio_decoder_t* const preferred = installed_decoder();
            preferred != nullptr && preferred->supports(format)) {
            return preferred->decode(path_copy, format);
        }
        for (audio_decoder_t* const decoder : default_decoders()) {
            if (decoder->supports(format)) {
                return decoder->decode(path_copy, format);
            }
        }
        throw std::runtime_error(std::string("load_audio: no installed decoder handles ") +
                                 std::string(format_name(format)) + " ('" + path_copy + "')");
    }
}
