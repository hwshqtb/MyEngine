#ifndef HWSHQTB__GUI__DETAIL__OFFSCREEN_SURFACE_H
#define HWSHQTB__GUI__DETAIL__OFFSCREEN_SURFACE_H

/// @file detail/offscreen_surface.h
/// @brief Internal: the ARGB8888 pixel buffer plus software renderer that stands in for a window
///        target (the single implementation behind image_t and offscreen_window_t).

#include <memory>

#include <SDL3/SDL.h>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"

namespace hwshqtb::gui {
    class paint_engine_t;
}

namespace hwshqtb::gui::detail {
    class sdl_paint_engine_t;

    /// @brief One ARGB8888 pixel buffer with a software renderer and a paint engine over it.
    ///
    /// The pixels live in an SDL_Surface; the renderer exists only so the SDL paint engine has
    /// something to draw through, which is what lets a device be painted and read back with no
    /// window, no display and no video subsystem. It is the ONE such implementation in the gui
    /// layer: image_t wraps it for offscreen images, and the offscreen platform window of a
    /// window_t wraps it as a stand-in for a native window target.
    ///
    /// @note Internal: no surface type appears in an installed header.
    /// @note The renderer's default target is this surface, so anything painted without an explicit
    ///       render target lands in the pixels read back by pixel().
    class offscreen_surface {
    public:
        /// @brief Allocate @p size ARGB8888 pixels and the software renderer over them.
        /// @param size The extent in pixels; both sides must be positive.
        /// @throws std::invalid_argument when either side is not positive.
        /// @throws std::runtime_error when SDL refuses the surface or the renderer.
        explicit offscreen_surface(extent_t size);

        ~offscreen_surface();

        offscreen_surface(const offscreen_surface&) = delete;
        offscreen_surface& operator=(const offscreen_surface&) = delete;
        offscreen_surface(offscreen_surface&&) = delete;
        offscreen_surface& operator=(offscreen_surface&&) = delete;

        /// @brief The extent of the allocated pixels.
        /// @note noexcept.
        extent_t size() const noexcept;

        /// @brief The engine that draws into this surface, or nullptr when there is none.
        /// @return The engine created with the pixels in the constructor, or nullptr for a surface
        ///         without pixels.
        /// @note A pure query: it never creates anything.
        paint_engine_t* paint_engine() noexcept;

        /// @brief The drawing target the gui layer passes around as an opaque void*.
        /// @return The SDL_Renderer* over this surface, type-erased so that no SDL type leaks into a
        ///         caller that only stores it (backing_store_t takes exactly this handle).
        /// @note The renderer belongs to this surface and dies with it; the pointer is stable for the
        ///       whole lifetime of the object.
        /// @note noexcept.
        void* target() const noexcept;

        /// @brief The colour of one pixel.
        /// @param x The pixel column.
        /// @param y The pixel row.
        /// @return The pixel colour; out-of-range coordinates answer transparent instead of reading
        ///         out of bounds.
        color_t pixel(int x, int y) const;

        /// @brief The whole pixel buffer, for one drawing call that needs to read it.
        /// @param pitch Receives the row pitch in PIXELS (never smaller than the width).
        /// @return The top-left pixel (ARGB, row-major), or nullptr for a surface without pixels.
        /// @note Queued drawing is flushed first, so the buffer holds everything painted so far.
        /// @note The pointer is valid until the next drawing call on this surface; a consumer that
        ///       keeps it beyond its own call is reading memory it no longer owns.
        const color_t* pixels(int& pitch) const;

        /// @brief The same buffer, writable (an image being loaded or built).
        /// @param pitch Receives the row pitch in PIXELS.
        /// @return The top-left pixel, or nullptr for a surface without pixels.
        color_t* pixels(int& pitch);

    private:
        /// @brief Apply everything the engine queued to the surface.
        void flush_engine() const;

        /// @brief The pixels; also the read-back source.
        SDL_Surface* _surface = nullptr;
        /// @brief The software renderer over @c _surface; the engine draws through it.
        SDL_Renderer* _renderer = nullptr;
        /// @brief The extent @c _surface was created with.
        extent_t _size{};
        /// @brief The engine over the pixels, created in the constructor.
        /// @note Declared last so it is destroyed first: it wraps @c _renderer.
        std::unique_ptr<sdl_paint_engine_t> _engine;
    };
}

#endif
