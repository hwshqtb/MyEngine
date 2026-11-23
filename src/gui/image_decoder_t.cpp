#include "hwshqtb/gui/image_decoder_t.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "detail/bmp.h"

namespace hwshqtb::gui {
    namespace {
        /// @brief The signature of a BMP: every bitmap starts with "BM".
        constexpr std::string_view bmp_magic = "BM";
        /// @brief The signature of a PNG: the 8-byte 0x89 'P' 'N' 'G' 0x0D 0x0A 0x1A 0x0A.
        constexpr std::string_view png_magic = "\x89PNG\r\n\x1A\n";
        /// @brief The first three bytes of every JPEG: 0xFF 0xD8 0xFF (SOI + the next marker's lead).
        constexpr std::string_view jpeg_magic = "\xFF\xD8\xFF";

        /// @brief The DECODER default: SDL's own format loaders, one call per sniffed format.
        ///
        /// @note It is a DEFAULT, not the interface: image_decoder_t is what the layer asks, and another
        ///       backend (a platform codec, a vendored decoder) can be installed with set_image_decoder().
        ///       SDL is chosen for the default because it is already the one dependency the gui layer has.
        class sdl_image_decoder_t final: public image_decoder_t {
        public:
            /// @brief SDL decodes the three formats the sniffing recognises.
            bool supports(image_format_enum format) const noexcept override {
                return format != image_format_enum::UNKNOWN;
            }

            /// @brief Dispatch to the matching SDL loader and normalise the result to ARGB8888.
            image_t decode(std::string_view path, image_format_enum format) override {
                const std::string path_copy{ path };
                SDL_Surface* raw = nullptr;
                const char* name = "unknown";
                switch (format) {
                case image_format_enum::BMP:
                    raw = SDL_LoadBMP(path_copy.c_str());
                    name = "bmp";
                    break;
                case image_format_enum::PNG:
                    raw = SDL_LoadPNG(path_copy.c_str());
                    name = "png";
                    break;
                case image_format_enum::JPEG:
                    raw = SDL_LoadJPG(path_copy.c_str());
                    name = "jpeg";
                    break;
                default:
                    break;
                }
                if (raw == nullptr) {
                    throw std::runtime_error(std::string("load_image: cannot decode ") + name + " '"
                                             + path_copy + "': " + SDL_GetError());
                }
                // One format reaches the paint engine: convert here so the blit path never branches.
                SDL_Surface* const converted = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
                SDL_DestroySurface(raw);
                if (converted == nullptr) {
                    throw std::runtime_error(std::string("load_image: cannot convert '") + path_copy
                                             + "' to ARGB8888: " + SDL_GetError());
                }
                image_t image{ extent_t{ converted->w, converted->h } };
                int target_pitch = 0;
                color_t* const target = image.writable_pixels(target_pitch);
                const auto* const source = static_cast<const color_t*>(converted->pixels);
                const int source_pitch = converted->pitch / static_cast<int>(sizeof(color_t));
                if (target != nullptr && source != nullptr) {
                    for (int row = 0; row < converted->h; ++row) {
                        std::memcpy(target + static_cast<std::ptrdiff_t>(row) * target_pitch,
                                    source + static_cast<std::ptrdiff_t>(row) * source_pitch,
                                    static_cast<std::size_t>(converted->w) * sizeof(color_t));
                    }
                }
                SDL_DestroySurface(converted);
                return image;
            }

            /// @brief The diagnostic name.
            std::string_view name() const noexcept override {
                return "sdl";
            }
        };

        /// @brief The default decoder, created on first use and never destroyed.
        /// @return The SDL backend, or nullptr when SDL is not available (it always is once an image is
        ///         loaded, because loading needs it).
        image_decoder_t* default_decoder() {
            static sdl_image_decoder_t decoder;
            return &decoder;
        }

        /// @brief The slot set_image_decoder() writes; nullptr means the default is used.
        image_decoder_t*& installed_decoder() noexcept {
            static image_decoder_t* installed = nullptr;
            return installed;
        }
    }

    image_decoder_t::~image_decoder_t() = default;

    image_format_enum image_format_of(std::string_view header) noexcept {
        if (header.size() >= png_magic.size() && header.substr(0, png_magic.size()) == png_magic) {
            return image_format_enum::PNG;
        }
        if (header.size() >= jpeg_magic.size() && header.substr(0, jpeg_magic.size()) == jpeg_magic) {
            return image_format_enum::JPEG;
        }
        if (header.size() >= bmp_magic.size() && header.substr(0, bmp_magic.size()) == bmp_magic) {
            return image_format_enum::BMP;
        }
        return image_format_enum::UNKNOWN;
    }

    void set_image_decoder(image_decoder_t* decoder) noexcept {
        installed_decoder() = decoder;
    }

    image_decoder_t* image_decoder() noexcept {
        image_decoder_t* const installed = installed_decoder();
        return installed != nullptr ? installed : default_decoder();
    }

    image_t load_image(std::string_view path) {
        // Sniff the magic bytes FIRST: an unknown file is reported as such, instead of being handed to a
        // decoder that can only answer "not a PNG".
        const std::string path_copy{ path };
        std::string header;
        if (std::FILE* const file = std::fopen(path_copy.c_str(), "rb")) {
            char bytes[8] = {};
            const std::size_t read = std::fread(bytes, 1, sizeof(bytes), file);
            std::fclose(file);
            header.assign(bytes, read);
        } else {
            throw std::runtime_error(std::string("load_image: cannot open '") + path_copy + "'");
        }
        const image_format_enum format = image_format_of(header);
        if (format == image_format_enum::UNKNOWN) {
            throw std::runtime_error(std::string("load_image: '") + path_copy
                                     + "' is not a BMP, PNG or JPEG (no known magic bytes)");
        }
        image_decoder_t* const decoder = image_decoder();
        if (decoder == nullptr || !decoder->supports(format)) {
            throw std::runtime_error(std::string("load_image: no installed decoder handles '") + path_copy
                                     + "'");
        }
        return decoder->decode(path_copy, format);
    }
}
