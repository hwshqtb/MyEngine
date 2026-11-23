#ifndef HWSHQTB__GUI__OFFSCREEN_WINDOW_H
#define HWSHQTB__GUI__OFFSCREEN_WINDOW_H

/// @file offscreen_window_t.h
/// @brief offscreen_window_t: a top-level window whose target is an offscreen pixel buffer.

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/window_t.h"

namespace hwshqtb::gui::detail {
    class offscreen_platform_window;

    /// @brief The one-shot construction hand-off between the two constructors of offscreen_window_t.
    /// @note A delegating constructor cannot read the raw pointer back out of the unique_ptr the base
    ///       class has adopted, so the pair travels through this small value instead.
    struct offscreen_window_seed;
}

namespace hwshqtb::gui {
    /// @brief A window_t whose platform half has no native window: it paints into memory.
    ///
    /// @par What it is for
    /// A real window_t needs a platform half. This class supplies one that owns an ARGB8888 pixel
    /// buffer plus a software renderer, so the WHOLE frame policy of window_t — damage bookkeeping,
    /// the per-rectangle pass decision, whole-surface presentation, scroll accumulation, capture — is
    /// exercised with no video subsystem, no display and no gui_application_t. It is a test device
    /// and a headless capture device, not a hidden window: nothing appears on any screen.
    ///
    /// @par Painting
    /// It is NOT final, and its paint_event() is the inherited empty one: a subclass overrides
    /// paint_event() exactly as it would for a real window, and reads the result back with pixel().
    /// The painter it receives is the ordinary painter_t over the ordinary paint engine, so a
    /// subclass draws the same code it would draw on a native window.
    ///
    /// @par Input
    /// There is no native event source: attach() installs no filter and the platform reports nothing
    /// of its own. Input is driven through the public platform entries — handle_input() for a step,
    /// handle_resize() / handle_focus() / handle_pointer_leave() for the lifecycle — which is what a
    /// headless test needs and what keeps window_t's input_state reachable without an OS.
    ///
    /// @par Lifetimes
    /// Like every window_t: create it after no application at all (this backend needs neither video
    /// nor an application object) and destroy it on the thread that paints. It must not outlive the
    /// images or painters that draw into it, exactly like a real window.
    class HWSHQTB_GUI_EXPORT offscreen_window_t: public window_t {
    public:
        /// @brief Create a window of @p size pixels painted into an offscreen buffer.
        /// @param size The client area in pixels; both sides must be positive.
        /// @throws std::invalid_argument when @p size is empty.
        /// @throws std::runtime_error when SDL cannot allocate the surface or its software renderer.
        /// @note The window starts hidden, like every window_t; show() paints the first frame.
        explicit offscreen_window_t(extent_t size);

        /// @brief Destroy the platform half (its backing store, engine, renderer and pixels).
        ~offscreen_window_t() override;

        /// @brief The colour of one pixel of the presented frame.
        /// @param x The pixel column.
        /// @param y The pixel row.
        /// @return The pixel colour, with the same shape as image_t::pixel(): out-of-range
        ///         coordinates answer transparent instead of reading out of bounds.
        /// @note The answer is the frame the window currently shows: the backing store is flushed into
        ///       the pixel buffer on every painted frame, which is the offscreen equivalent of a
        ///       present.
        /// @note noexcept.
        color_t pixel(int x, int y) const noexcept;

    private:
        /// @brief Adopt the platform half built by the public constructor.
        /// @param seed The owned platform (handed to window_t) and the raw pointer the pixel query
        ///        keeps.
        explicit offscreen_window_t(detail::offscreen_window_seed seed);

        /// @brief The platform half this window owns, borrowed for the pixel query.
        /// @note window_t owns it (std::unique_ptr<platform_window_t>); this pointer neither owns nor
        ///       outlives it, and is only read while the object is alive.
        detail::offscreen_platform_window* _platform = nullptr;
    };
}

#endif
