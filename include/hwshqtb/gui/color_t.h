#ifndef HWSHQTB__GUI__COLOR_H
#define HWSHQTB__GUI__COLOR_H

/// @file color_t.h
/// @brief The RGBA colour value type of the GUI layer and its 32-bit ARGB packaging.

#include <cstdint>

namespace hwshqtb::gui {
    /// @brief A colour with 8-bit red, green, blue and alpha channels (the QColor value subset).
    ///
    /// @par Pixel format
    /// The GUI layer stores pixels as SDL_PIXELFORMAT_ARGB8888, whose 32-bit word on a little-endian
    /// machine is 0xAARRGGBB (the bytes in memory are A, R, G, B). The H0 stage measured that packing
    /// and it is load-bearing:
    /// color_t::red_color().to_argb() == 0xFFFF0000, whereas the "natural" RGB byte order would give
    /// 0xFF0000FF. to_argb() / from_argb() are the only conversion points.
    ///
    /// @par Header-only by design
    /// color_t is a constexpr value type, so like geometry.h it is the one sanctioned exception to
    /// the project rule "declarations in headers, bodies in .cpp".
    ///
    /// @par Named colours
    /// Every named colour is a static factory named `<name>_color`, the convention for a class's
    /// static instances: black_color(), white_color(), red_color(), ... The suffix is
    /// not reserved for the three names that would otherwise collide with a channel accessor (red,
    /// green, blue); the rule is uniform, so a named colour is recognisable at the call site and the
    /// channel accessors keep their natural names (red(), green(), blue(), alpha()).
    class color_t {
        std::uint8_t _r = 0;
        std::uint8_t _g = 0;
        std::uint8_t _b = 0;
        std::uint8_t _a = 255;

    public:
        /// @brief Construct opaque black (0, 0, 0, alpha 255).
        constexpr color_t() noexcept = default;

        /// @brief Construct from the four channels.
        /// @param r The red channel.
        /// @param g The green channel.
        /// @param b The blue channel.
        /// @param a The alpha channel; 255 (opaque) when omitted.
        constexpr color_t(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) noexcept:
            _r(r),
            _g(g),
            _b(b),
            _a(a) { }

        /// @brief The red channel.
        constexpr std::uint8_t red() const noexcept {
            return _r;
        }

        /// @brief The green channel.
        constexpr std::uint8_t green() const noexcept {
            return _g;
        }

        /// @brief The blue channel.
        constexpr std::uint8_t blue() const noexcept {
            return _b;
        }

        /// @brief The alpha channel (0 transparent, 255 opaque).
        constexpr std::uint8_t alpha() const noexcept {
            return _a;
        }

        /// @brief Opaque black (0x000000, alpha 255).
        static constexpr color_t black_color() noexcept {
            return color_t{ 0, 0, 0 };
        }

        /// @brief Opaque white (0xFFFFFF, alpha 255).
        static constexpr color_t white_color() noexcept {
            return color_t{ 255, 255, 255 };
        }

        /// @brief Opaque red (0xFF0000, alpha 255).
        /// @note red_color() rather than red() because red() is the channel accessor.
        static constexpr color_t red_color() noexcept {
            return color_t{ 255, 0, 0 };
        }

        /// @brief Opaque green (0x00FF00, alpha 255).
        /// @note green_color() rather than green() because green() is the channel accessor.
        static constexpr color_t green_color() noexcept {
            return color_t{ 0, 255, 0 };
        }

        /// @brief Opaque blue (0x0000FF, alpha 255).
        /// @note blue_color() rather than blue() because blue() is the channel accessor.
        static constexpr color_t blue_color() noexcept {
            return color_t{ 0, 0, 255 };
        }

        /// @brief Opaque yellow (0xFFFF00, alpha 255).
        static constexpr color_t yellow_color() noexcept {
            return color_t{ 255, 255, 0 };
        }

        /// @brief Opaque cyan (0x00FFFF, alpha 255).
        static constexpr color_t cyan_color() noexcept {
            return color_t{ 0, 255, 255 };
        }

        /// @brief Opaque magenta (0xFF00FF, alpha 255).
        static constexpr color_t magenta_color() noexcept {
            return color_t{ 255, 0, 255 };
        }

        /// @brief Opaque mid gray (0x808080, alpha 255).
        static constexpr color_t gray_color() noexcept {
            return color_t{ 128, 128, 128 };
        }

        /// @brief Fully transparent black (0, 0, 0, alpha 0).
        static constexpr color_t transparent_color() noexcept {
            return color_t{ 0, 0, 0, 0 };
        }

        /// @brief Construct an opaque colour from a 0xRRGGBB word.
        /// @param rgb The packed colour; only the low 24 bits are used, the high byte is ignored.
        /// @return The colour with the given channels and alpha 255.
        static constexpr color_t from_rgb(std::uint32_t rgb) noexcept {
            return color_t{ static_cast<std::uint8_t>((rgb >> 16) & 0xFFu),
                            static_cast<std::uint8_t>((rgb >> 8) & 0xFFu),
                            static_cast<std::uint8_t>(rgb & 0xFFu) };
        }

        /// @brief Construct from an SDL_PIXELFORMAT_ARGB8888 word.
        /// @param argb The packed 0xAARRGGBB word; for example 0xFFFF0000 is opaque red.
        /// @return The unpacked colour.
        static constexpr color_t from_argb(std::uint32_t argb) noexcept {
            return color_t{ static_cast<std::uint8_t>((argb >> 16) & 0xFFu),
                            static_cast<std::uint8_t>((argb >> 8) & 0xFFu),
                            static_cast<std::uint8_t>(argb & 0xFFu),
                            static_cast<std::uint8_t>((argb >> 24) & 0xFFu) };
        }

        /// @brief Pack into the SDL_PIXELFORMAT_ARGB8888 word 0xAARRGGBB.
        ///
        /// On a little-endian machine this is the word SDL expects for ARGB8888 (the memory byte
        /// order is A, R, G, B). The H0 probe measured the format, so the packing is fixed:
        /// @code
        /// color_t::red_color().to_argb()        == 0xFFFF0000
        /// color_t::white_color().to_argb()      == 0xFFFFFFFF
        /// color_t::transparent_color().to_argb() == 0x00000000
        /// @endcode
        /// @return The 32-bit ARGB word.
        constexpr std::uint32_t to_argb() const noexcept {
            return (static_cast<std::uint32_t>(_a) << 24) | (static_cast<std::uint32_t>(_r) << 16)
                 | (static_cast<std::uint32_t>(_g) << 8) | static_cast<std::uint32_t>(_b);
        }

        /// @brief Whether the colour is fully opaque (alpha() == 255).
        constexpr bool is_opaque() const noexcept {
            return _a == 255;
        }

        /// @brief Whether all four channels are equal.
        friend constexpr bool operator==(color_t a, color_t b) noexcept {
            return a._r == b._r && a._g == b._g && a._b == b._b && a._a == b._a;
        }
    };
}

#endif
