/// @file detail/bmp.cpp
/// @brief The one BMP writer both platform windows share.

#include "bmp.h"

#include <stdexcept>
#include <string>

namespace hwshqtb::gui::detail {
    void write_bmp(SDL_Surface* raw, std::string_view path, std::string_view api) {
        SDL_Surface* surface = raw;
        if (raw->format != SDL_PIXELFORMAT_BGR24) {
            surface = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_BGR24);
            SDL_DestroySurface(raw);
            if (surface == nullptr) {
                throw std::runtime_error(std::string(api) + ": SDL_ConvertSurface failed: "
                                         + SDL_GetError());
            }
        }
        const std::string file{ path };
        const bool saved = SDL_SaveBMP(surface, file.c_str());
        SDL_DestroySurface(surface);
        if (!saved) {
            throw std::runtime_error(std::string(api) + ": SDL_SaveBMP failed: " + SDL_GetError());
        }
    }
    SDL_Surface* load_bmp(std::string_view path) {
        const std::string path_copy{ path };
        SDL_Surface* const raw = SDL_LoadBMP(path_copy.c_str());
        if (raw == nullptr) {
            return nullptr;
        }
        // One known format reaches the paint engine: a 24-bit or paletted file is converted here so the
        // blit path never has to branch on the source format.
        SDL_Surface* const converted =
            SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
        SDL_DestroySurface(raw);
        return converted;
    }
}