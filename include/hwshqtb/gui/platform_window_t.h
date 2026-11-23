#ifndef HWSHQTB__GUI__PLATFORM_WINDOW_H
#define HWSHQTB__GUI__PLATFORM_WINDOW_H

/// @file platform_window_t.h
/// @brief platform_window_t: the platform half of a top-level window (the QPlatformWindow role).

#include <cstdint>
#include <string_view>

#include "hwshqtb/gui/backing_store_t.h"
#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/geometry.h"

namespace hwshqtb::gui {
    class paint_engine_t;
    class window_t;

    /// @brief The platform half of a top-level window: the native window, its renderer, its paint
    ///        engine and the platform event source.
    ///
    /// @par The three roles
    /// A top-level window is split the way Qt splits QWindow:
    ///  - gui::window_t is the PUBLIC window: it owns the frame policy (damage, partial passes,
    ///    presentation, scroll accumulation, input state, capture) and is the paint device a painter
    ///    draws into.
    ///  - platform_window_t (this class) is the PLATFORM half: it creates the native window and its
    ///    renderer, and it translates the platform's events into backend-independent steps.
    ///  - ui::widget_window_t is the ITEM-TREE HOST: it is not a window at all, it holds a
    ///    gui::window_t and paints a widget tree into it.
    ///
    /// @par The surface belongs to the store
    /// The persistent surface a frame paints into is NOT part of this interface: it is a
    /// backing_store_t, exactly as in Qt, where QBackingStore owns beginPaint/endPaint/flush/scroll
    /// and QPlatformWindow merely hands the store out (QPlatformWindow::backingStore()). So this
    /// interface has no begin_frame() / end_frame() / surface_ready() / scroll_surface(): the window
    /// drives the store directly and this object only answers "here it is, or nullptr when the driver
    /// refused it".
    ///
    /// @par No platform type escapes
    /// This header is the boundary: no native handle, no backend event and no backend key type appears
    /// in it. Everything crosses as a value type of this layer (extent_t, rect_t, point_t, int,
    /// std::string_view, interaction_t), so a window_t can be driven by any platform — a test supplies
    /// a fake implementation and gets the whole frame policy with no window system at all.
    ///
    /// @par Who calls whom
    /// window_t calls the virtuals below to create, show, paint and capture. The platform calls BACK
    /// into the window through window_t's documented report entries (handle_resize(), handle_input(),
    /// ..., handle_expose(), repair_frame()) when its event source sees something; the direction of
    /// every call is part of the contract, and a platform implementation never posts an event object
    /// itself.
    ///
    /// @par Lifetime
    /// One implementation object per window for the window's whole life. window_t owns it
    /// (std::unique_ptr) and destroys it after its own frame state is gone; the implementation's
    /// attach() installs the native event filter and its destructor removes it, so no report can
    /// reach a half-destroyed window. The store the implementation hands out lives at least as long as
    /// the implementation, and therefore outlives every frame that paints into it.
    ///
    /// @par Construction
    /// The constructor is protected: an implementation is created through the platform factory
    /// (detail::create_platform_window) or injected by a test, never named by application code. A
    /// window is single-threaded: create, paint and destroy it on the thread that runs the event
    /// loop.
    class HWSHQTB_GUI_EXPORT platform_window_t {
    public:
        virtual ~platform_window_t();

        platform_window_t(const platform_window_t&) = delete;
        platform_window_t& operator=(const platform_window_t&) = delete;
        platform_window_t(platform_window_t&&) = delete;
        platform_window_t& operator=(platform_window_t&&) = delete;

        /// @brief Show the window, raise it and bring the platform text input in line with its focus.
        /// @throws std::runtime_error when the platform refuses to show it (the message carries the
        ///         platform's error text).
        virtual void show() = 0;

        /// @brief Hide the window and stop the platform text input.
        /// @throws std::runtime_error when the platform refuses to hide it.
        virtual void hide() = 0;

        /// @brief Whether the window is currently shown.
        /// @note noexcept.
        virtual bool is_visible() const noexcept = 0;

        /// @brief Change the window title.
        /// @throws std::runtime_error when the platform refuses the change.
        virtual void set_title(std::string_view title) = 0;

        /// @brief Tell the window system the smallest size this window may be given.
        /// @param size The minimum CLIENT size, in the window coordinates size() reports; a zero or
        ///        negative component means "no floor on that axis".
        /// @note The window manager enforces it: the user cannot drag the window smaller, which is how a
        ///       tree whose widgets have a minimum size stops being squeezed into overlapping text.
        /// @note A backend without a resizable window (an off-screen device) ignores it.
        /// @note noexcept: a window system that refuses the hint is not an error — the window simply keeps
        ///       no floor, and the next call retries.
        virtual void set_minimum_size(::hwshqtb::gui::extent_t size) noexcept = 0;

        /// @brief The backend's window id: what every native event of this window carries.
        /// @return A plain integer. window_t::window_id() forwards it, so a host can tag an injected
        ///         event with it without naming a platform type.
        /// @note noexcept. Stable for the window's lifetime.
        virtual std::uint32_t native_id() const noexcept = 0;

        /// @brief The device size in PIXELS: the renderer's output size, the space a frame draws into.
        /// @note noexcept. Queried live, so it already reports a resize whose event is still pending.
        virtual extent_t device_size() const noexcept = 0;

        /// @brief The client area in PIXELS: the backing surface's grid and the presented area.
        /// @note noexcept. Equals device_size() on a backend whose renderer output is the client area.
        /// @note It is also the size window_t calls backing_store_t::resize() with, every frame.
        virtual extent_t pixel_size() const noexcept = 0;

        /// @brief The client area as a rectangle in WINDOW coordinates: its position on screen (the
        ///        payload of a move event) and its extent in the space the platform reports pointer
        ///        positions in.
        /// @return rect_t{ window position, client area in window coordinates }. The position is in
        ///         global screen coordinates; the extent is not a pixel count.
        /// @note noexcept. window_t converts a pointer position from this space with the pair
        ///       (window_t::window_coordinate_size(), window_t::pixel_size()); the platform never
        ///       converts, it only reports.
        ///
        /// @par An unplaced window: the EMPTY rectangle
        /// A backend whose window is not on any screen (an offscreen target, a headless one) has no
        /// position to report, and says so with an EMPTY rectangle — a size of -1 x -1, or any
        /// non-positive size. That is the emptiness rect_t already defines (isEmpty() is
        /// `width() <= 0 || height() <= 0`), so "no geometry" needs no sentinel position, no
        /// optional<rect_t> and no extra query. window_t then treats the window as having no position
        /// (it reports no move) and as having no window-coordinate extent (the pointer conversion is
        /// the identity, which is what an unknown density must produce).
        /// @note A POSITION sentinel would be wrong here: (-1, -1) is a legal window position on a
        ///       multi-monitor desktop, while a negative client AREA is impossible for a real window —
        ///       so the size is the one field that can carry "none" unambiguously.
        virtual rect_t window_geometry() const noexcept = 0;

        /// @brief The paint engine over this window's renderer, or nullptr when it has none.
        /// @return The backend the device draws through, created with the render target.
        /// @note A PURE QUERY: it never creates anything and never throws — the same contract as
        ///       paint_device_t::paint_engine().
        /// @note noexcept.
        virtual paint_engine_t* paint_engine() noexcept = 0;

        /// @brief The persistent surface this window paints between presents, or nullptr when the
        ///        driver refused it (the window then paints straight into its window target).
        /// @return The store window_t drives: resize(), begin_paint(), end_paint(), flush(), scroll()
        ///         and is_valid() are the store's, not this object's.
        /// @note A PURE QUERY: the store is created once, with the render target, and the same pointer
        ///       answers for the window's whole life. It never creates anything and never throws.
        /// @note noexcept.
        virtual backing_store_t* backing_store() noexcept = 0;

        /// @brief Present the current window target.
        /// @note A failed present is not fatal and is ignored: a minimized or hidden window may refuse
        ///       to present, and the next frame tries again.
        /// @note A flush() of the store presents as well; this call is for the frames that were painted
        ///       straight into the window target.
        virtual void present() = 0;

        /// @brief Read the CURRENT render target back, normalise it to 24-bit BGR and write a BMP.
        /// @param path The output file path.
        /// @throws std::runtime_error when the pixels cannot be read back or the file cannot be written
        ///         (the message carries the platform's error text).
        /// @note window_t owns the target switching: it calls this while the backing store is the
        ///       render target (a capture of the painted frame) or before presenting a direct-to-window
        ///       frame. The platform only reads whatever target is current, so no target-selection flag
        ///       crosses this boundary.
        /// @note The pixels are normalised before the write, so the file is byte-identical whichever
        ///       path produced the frame (windowed, dummy or offscreen).
        virtual void write_frame(std::string_view path) = 0;

        /// @brief Turn the platform text input on or off for this window.
        /// @param enabled true to start it, false to stop it (and hide any screen keyboard it showed).
        /// @note noexcept. Idempotent. A platform produces no committed text at all until the input is
        ///       started, which is why the platform keeps it in line with its own keyboard focus.
        /// @note The focus itself is NOT part of this interface: nothing above the platform can ask
        ///       "does this window have the keyboard" without a native event source to answer it, and
        ///       window_t never asks. A backend that needs it (to decide whether to start text input)
        ///       keeps the query to itself.
        virtual void set_text_input(bool enabled) noexcept = 0;

        /// @brief Attach this platform window to its window: set the report target, install the native
        ///        event filter and start the text input in line with the current focus.
        /// @param owner The window every report goes to; it outlives the platform object.
        /// @note Called exactly once, by the factory or by the window_t constructor that takes an
        ///       injected platform window. The filter is INSTALLED here and REMOVED in the destructor,
        ///       so the reports stop before the window starts dying.
        /// @note It is a separate call on purpose, not part of the constructor: a test's injected
        ///       platform has to exist BEFORE the window that adopts it, so construction can never be
        ///       the moment the two are wired together.
        virtual void attach(window_t& owner) = 0;

        /// @brief Ask the platform to take over the repaint of the next frame.
        /// @return true when the platform owns the frame schedule right now and will paint the frame
        ///         itself; false when the caller has to post the usual PAINT request.
        /// @note The Windows size-move loop blocks the event pump: a posted PAINT would wait until the
        ///       drag ends, so the platform marks the frame dirty and its 16 ms timer paints it with
        ///       window_t::repair_frame().
        /// @note noexcept. A platform without such a loop answers false unconditionally.
        virtual bool defer_repaint() noexcept = 0;

    protected:
        /// @brief Constructor; only an implementation (or the platform factory) may create one.
        /// @note noexcept.
        platform_window_t() = default;
    };
}

#endif
