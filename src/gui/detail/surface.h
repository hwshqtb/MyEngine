#ifndef HWSHQTB__GUI__DETAIL__SURFACE_H
#define HWSHQTB__GUI__DETAIL__SURFACE_H

/// @file detail/surface.h
/// @brief Internal: the SDL pixel surface behind a window's backing store.

#include <memory>

#include <SDL3/SDL.h>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/region_t.h"

namespace hwshqtb::gui {
    class paint_engine_t;
}

namespace hwshqtb::gui::detail {
    /// @brief The SDL bound surface: an ARGB8888 render-target texture of an existing renderer.
    ///
    /// It is the persistent pixel buffer a window paints into between presents: the texture is the
    /// render target, and flush() copies it to the renderer's own (window) target and presents. A
    /// driver that cannot render into an offscreen texture is refused once and then remembered, so the
    /// window falls back to painting the target directly.
    ///
    /// @note The texture cannot be its own copy source while it is the render target, so scroll()
    ///       goes through one scratch texture of the scrolled rectangle's size.
    /// @note This is an internal implementation of backing_store_t: it is not part of the public API
    ///       and no surface type appears in an installed header.
    class sdl_bound_surface_t {
    public:
        /// @brief Wrap @p target and allocate @p size pixels when the size is not empty.
        /// @param size The initial extent; an empty extent allocates nothing (resize() does).
        /// @param target The SDL_Renderer*, type-erased to void*; not owned, nullptr creates a
        ///        surface that stays invalid.
        sdl_bound_surface_t(extent_t size, void* target);

        ~sdl_bound_surface_t();

        sdl_bound_surface_t(const sdl_bound_surface_t&) = delete;
        sdl_bound_surface_t& operator=(const sdl_bound_surface_t&) = delete;

        /// @brief Make the surface match @p size, recreating it when the size changes.
        /// @param size The new extent; an empty extent frees the pixels.
        /// @return true when a valid surface of that size exists afterwards.
        /// @note After a driver refused the render target once, this keeps answering false.
        bool resize(extent_t size);

        /// @brief Whether the surface exists and the renderer accepted it as a render target.
        /// @note noexcept.
        bool is_valid() const noexcept;

        /// @brief The surface extent; an empty extent when there is no surface.
        /// @note noexcept.
        extent_t size() const noexcept;

        /// @brief Make the surface the renderer's target.
        /// @return true when the surface is the current render target.
        bool begin_paint();

        /// @brief Restore the renderer's default target (a no-op without an open pass).
        void end_paint();

        /// @brief Copy @p region of the surface to the window target and present.
        void flush(const region_t& region);

        /// @brief Move the surface's own pixels inside @p rect by (@p dx, @p dy).
        /// @return true when the pixels moved; false when the caller must repaint @p rect instead.
        bool scroll(rect_t rect, int dx, int dy);

    private:
        /// @brief Destroy the surface and its scratch copy, forgetting their sizes (idempotent).
        void destroy_texture() noexcept;

        /// @brief Make sure a scratch texture of @p size exists for the scroll copy.
        /// @return true when the scratch texture covers @p size.
        bool ensure_scratch(extent_t size);

        /// @brief The renderer the surface belongs to; not owned (the window owns it).
        SDL_Renderer* _renderer = nullptr;
        /// @brief The persistent surface; null until resize() succeeded.
        SDL_Texture* _texture = nullptr;
        /// @brief The pixel size @c _texture was created with.
        extent_t _size{};
        /// @brief Whether begin_paint() opened a pass that end_paint() has not closed.
        bool _painting = false;
        /// @brief Whether the driver already refused a render-target switch (no more retries).
        bool _refused = false;
        /// @brief The scratch copy used by scroll(); the surface cannot be its own source.
        SDL_Texture* _scratch = nullptr;
        /// @brief The pixel size @c _scratch was created with.
        extent_t _scratch_size{};
    };
}

#endif
