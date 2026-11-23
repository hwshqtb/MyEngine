#ifndef HWSHQTB__GUI__DETAIL__OFFSCREEN_PLATFORM_WINDOW_H
#define HWSHQTB__GUI__DETAIL__OFFSCREEN_PLATFORM_WINDOW_H

/// @file detail/offscreen_platform_window.h
/// @brief Internal: the platform_window_t implementation with no native window behind it.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "hwshqtb/gui/backing_store_t.h"
#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/platform_window_t.h"
#include "offscreen_surface.h"

namespace hwshqtb::gui::detail {
    /// @brief A platform window whose "native window" is an offscreen pixel buffer.
    ///
    /// It exists to prove that platform_window_t does not secretly require a real window: the whole
    /// frame policy of window_t (damage, passes, presentation, scroll, capture) runs over this object
    /// with no video subsystem, no display, no GUI application and no native event source.
    ///
    /// @par What it can and cannot answer
    /// - The pixels, the renderer, the engine and the backing store are REAL: the store wraps this
    ///   surface's software renderer, so a frame is painted, flushed and read back exactly as it is
    ///   for a native window.
    /// - show/hide only flip a flag, and set_title only remembers a string: there is nothing to show.
    /// - There is no event source at all, so attach() installs no filter and set_text_input() does
    ///   nothing; a test drives input through window_t::handle_input() directly.
    /// - native_id() is 0: this window belongs to no native event stream.
    ///
    /// @note Internal: created by gui::offscreen_window_t, never named by application code.
    class offscreen_platform_window final: public ::hwshqtb::gui::platform_window_t {
    public:
        /// @brief Allocate @p size pixels and the backing store over their renderer.
        /// @param size The client area in pixels; both sides must be positive.
        /// @throws std::invalid_argument when @p size is empty.
        /// @throws std::runtime_error when SDL refuses the surface or its software renderer.
        explicit offscreen_platform_window(extent_t size);

        ~offscreen_platform_window() override;

        offscreen_platform_window(const offscreen_platform_window&) = delete;
        offscreen_platform_window& operator=(const offscreen_platform_window&) = delete;

        void show() override;
        void hide() override;
        bool is_visible() const noexcept override;
        void set_title(std::string_view title) override;
        void set_minimum_size(::hwshqtb::gui::extent_t size) noexcept override;
        std::uint32_t native_id() const noexcept override;

        extent_t device_size() const noexcept override;
        extent_t pixel_size() const noexcept override;
        rect_t window_geometry() const noexcept override;

        paint_engine_t* paint_engine() noexcept override;
        backing_store_t* backing_store() noexcept override;
        void present() override;
        void write_frame(std::string_view path) override;

        void set_text_input(bool enabled) noexcept override;
        void attach(::hwshqtb::gui::window_t& window) override;
        bool defer_repaint() noexcept override;

        /// @brief The colour of one pixel of the presented frame.
        /// @param x The pixel column.
        /// @param y The pixel row.
        /// @return The pixel colour; out-of-range coordinates answer transparent.
        /// @note This is the read-back an offscreen window offers instead of a screen: the backing
        ///       store is flushed into this surface on every painted frame, so the pixels are exactly
        ///       what a native window would show.
        color_t pixel(int x, int y) const;

    private:
        /// @brief The offscreen pixels and their software renderer (the "window target").
        offscreen_surface _surface;
        /// @brief The persistent surface the frames paint into; wraps @c _surface's renderer.
        /// @note Declared after @c _surface so it is destroyed first: it holds a texture of that
        ///       renderer.
        std::unique_ptr<backing_store_t> _store;
        /// @brief The window every report would go to; kept for symmetry with a real platform.
        ::hwshqtb::gui::window_t* _owner = nullptr;
        /// @brief The show/hide flag (there is nothing to display).
        bool _visible = false;
        /// @brief The title a caller asked for; kept only so a debugger shows something meaningful.
        std::string _title;
    };
}

#endif
