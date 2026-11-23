#ifndef HWSHQTB__GUI__WINDOW_H
#define HWSHQTB__GUI__WINDOW_H

/// @file window_t.h
/// @brief window_t: a top-level window and paint device (the QWidget top-level subset), plus the
///        contract its platform half implements.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/input_state_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/paint_device_t.h"
#include "hwshqtb/gui/platform_window_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/gui/window_event_t.h"
#include "hwshqtb/metaobject/object_t.h"

namespace hwshqtb::metaobject {
    class event_t;
}

namespace hwshqtb::gui {
    class painter_t;

    /// @brief A top-level window: a native window plus a renderer, painted through the same
    ///        paint_engine_t seam as image_t, with QWidget-like paint/resize/close/key callbacks.
    ///
    /// @par The three roles
    /// A window is split the way Qt splits QWindow:
    ///  - THIS class is the public window and the policy half: it owns the frame schedule (update() /
    ///    repaint()), the damage bookkeeping, the partial-pass decision, the scroll accumulation, the
    ///    input state and the capture path. It is concrete and usable on its own — examples/hello_gui
    ///    subclasses it and draws, with no widget layer involved.
    ///  - platform_window_t is the PLATFORM half: the native window, its renderer, its persistent
    ///    surface, the platform event source and the translation of native events into the reports
    ///    below. window_t owns one through std::unique_ptr<platform_window_t> and never names a
    ///    platform type.
    ///  - ui::widget_window_t is the ITEM-TREE HOST: it is NOT a window subclass any more. It holds a
    ///    window_t and paints a widget tree into it, so the widget layer composes a window instead of
    ///    inheriting one.
    ///
    /// @par Device and backend
    /// window_t is a paint_device_t: painter_t draws into it exactly as it draws into image_t, and the
    /// device hands out the same backend implementation the platform created over the native renderer.
    /// That reuse is the point of the engine seam: one painter API and one engine implementation serve
    /// offscreen tests and the real window, so a test's pixel assertions cover the same drawing code
    /// the window runs.
    ///
    /// @par Event loop driven (no busy loop)
    /// The library's event loop repaints the window; the application never polls the platform itself.
    /// The platform half correlates the native events by the window they belong to and calls the
    /// report entries below, which QUEUE the window events on this thread's dispatcher:
    ///  - exposure             -> expose_event_t
    ///  - size change          -> resize_event_t(old size, new size)
    ///  - move                 -> move_event_t(old position, new position)
    ///  - close request        -> close_event()
    ///  - focus gained         -> the platform text input is started
    ///  - focus lost           -> the platform text input is stopped and the held-key set is cleared
    ///  - key press            -> key_press_event(key)
    /// The reports only observe: nothing is swallowed, so a system quit still reaches the dispatcher
    /// and the loop still exits. The queued events are delivered on a later loop round to
    /// resize_event(), move_event() or expose_event(); a resize or an expose also schedules ONE
    /// repaint through update(). update() is the Qt QWidget::update model: it marks a repaint pending,
    /// posts one paint event (event_type_enum::PAINT) to this thread's event dispatcher, and the
    /// loop's next iteration dispatches it, which clears the frame, calls paint_event() and presents.
    /// Repeated update() calls before that iteration coalesce into one repaint.
    ///
    /// @par Platform text input
    /// A platform produces no committed text until the application enables text input for a window;
    /// it is off by default and the state is PER WINDOW. window_t turns it on when the window gains the
    /// keyboard focus (handle_focus(true) -> platform_window_t::set_text_input(true)) and off when the
    /// window loses it (handle_focus(false)), and hide(), close() and destruction turn it off as well,
    /// so a hidden, closed or dead window never keeps an input method or a screen keyboard alive.
    /// The constructor and show() also start it when the window already owns the focus, and every call
    /// is idempotent, so a focus-loss report that follows an explicit hide cannot double-toggle it.
    ///
    /// This layer does not know whether any control wants text. It follows Qt's window-level rule: a
    /// focused window owns the keyboard, so text input runs for the whole window and the ui layer
    /// routes the delivered text to its focused item. Starting text input may activate an input method
    /// or show a screen keyboard, and on some platforms that can delay or swallow key presses, which
    /// is why it follows the focus instead of staying on for the window's whole life. A control that
    /// needs text must not look for keys itself: text arrives only through input_state_t::text_input().
    ///
    /// Key codes and text are separate channels and coexist: key reports still drive input_state_t's
    /// held keys and key_press_event() (Backspace, Delete, the arrows, Tab and the shortcuts), while a
    /// committed-text report records the platform's characters. Text is never derived from a key code.
    /// IME composition is not forwarded: only the committed text reaches the controls.
    ///
    /// @par Compression
    /// A pending resize_event_t keeps its EARLIEST old size and takes the LATEST new size when a later
    /// resize of the same window is folded into it; pending expose events fold into the first one. The
    /// dispatcher performs that folding only while event compression is enabled, so the default (off)
    /// delivers every queued window event.
    ///
    /// @par Platform modal loops
    /// While a platform drags/resizes a window it may run its own modal loop inside the message pump;
    /// the event pump is then not re-entered, so the event loop would stall. The platform half handles
    /// that itself: it reports update() as accepted through platform_window_t::defer_repaint(), marks
    /// the frame dirty, and paints repair frames from its own timer through handle_resize(true) and
    /// repair_frame() — WITHOUT posting events and WITHOUT consuming the input edges.
    ///
    /// @par Frames are independent
    /// Every repaint starts from the default paint state: the painter is created fresh (opaque black
    /// colour, no clip, identity transform) and paint_engine_t::begin() resets the backend's blend
    /// mode, clip and scale. The painted rectangle is cleared to white before paint_event(), so an
    /// override can never inherit colour, clip or transform from the previous frame.
    ///
    /// @par Size and coordinate spaces
    /// One frame touches three extents, and they are not the same concept:
    ///  - size() is the device size in PIXELS: the renderer's output size
    ///    (platform_window_t::device_size()). It is the space the item tree, geometry, damage, painting
    ///    and the painter's default clip all live in: the ui layer sizes its root widget from it and
    ///    every damage rectangle is in it.
    ///  - the window's pixel size (platform_window_t::pixel_size()) is the pixel grid of the client
    ///    area. It is the backing surface's size, the region a painted frame copies to the window (one
    ///    1:1 blit), and what scroll() and paint_frame() clip a rectangle to.
    ///  - the window's size (platform_window_t::window_geometry()) is the client area in WINDOW
    ///    coordinates, the space the platform reports pointer positions in. It is the only one of the
    ///    three that is not a pixel count, and it is the space the painter cannot use directly.
    /// On a window renderer the first two are the same number (a renderer without an output size of
    /// its own falls back to the window's pixel size), so a frame's drawing target, its damage
    /// rectangles and its backing surface all agree — the invariant paint_frame() relies on. The third
    /// differs from them by exactly the window's pixel density; the backend applies that factor when it
    /// maps a pointer position, and window_t applies it in exactly one place (see below). On Windows
    /// the client rectangle IS the pixel grid (one DPI-aware coordinate space), so the density is 1 and
    /// the three coincide; a HiDPI backend (a Retina display, a fractionally scaled Wayland compositor)
    /// reports a density above 1, and a pointer position is then in a space 1.5 (or 2) times smaller
    /// than the tree's.
    ///
    /// @par Pointer positions
    /// The pointer is the one input that crosses spaces: the platform reports it in WINDOW coordinates,
    /// while every rectangle it is compared with is in the pixel space above. The window converts it in
    /// exactly ONE place — pixel_point_of(), used by every pointer report — through
    /// window_to_pixel_point(): x * pixel_size() / window_coordinate_size() per axis, rounded DOWN to
    /// the pixel cell that contains the point. The platform never converts; it reports the raw window
    /// point. When the two extents agree (a density of 1) the conversion is the identity, so the common
    /// case is unchanged; when they differ, the converted point lands on the same grid the item tree,
    /// damage and painting use.
    ///
    /// @par Partial repaint
    /// A frame paints the window's pending_damage() only: the damage region is painted one pass per
    /// rectangle, each clipped to that rectangle and nothing else, and a frame with no damage paints
    /// nothing at all (no clear, no paint_event(), no flush, no present). A backend that can clip to a
    /// REGION (engine_feature_flag::CLIP_REGION) is painted in ONE pass for the whole region instead;
    /// the pixels are identical either way. The pixels outside the damaged rectangles keep whatever the
    /// persistent backing surface holds from the previous frame, which is what makes a window at rest
    /// cost no GPU work. Only the frames that cannot be partial — the first one, a resize, an expose,
    /// the platform's size-move repair frame, a capture, and a window without a backing surface —
    /// repaint the whole client area.
    ///
    /// @par Partial painting, whole presentation
    /// The window target is not persistent: a flip-model swap chain gives a normal window two buffers,
    /// so the buffer a frame draws into holds the pixels of the frame two presents ago, and only the
    /// backing surface survives a present. A painted frame therefore copies the WHOLE surface to the
    /// target (presented_region()) even though it painted one region: a scroll moves pixels the damaged
    /// strip does not cover, and a hover band painted two frames ago would otherwise come back. The
    /// saving is in the painting, not in the copy — one clipped pass per damaged rectangle, no tree
    /// work at all for a window at rest, and one 1:1 surface blit per frame.
    ///
    /// @par The store owns the surface
    /// The persistent surface is a backing_store_t handed out by platform_window_t::backing_store(),
    /// exactly as a QBackingStore is handed out by QPlatformWindow: the pixels, the render-target switch
    /// and the copy belong to the store, and this class decides WHEN each step runs — resize() before
    /// the frame, begin_paint() / end_paint() around the passes, flush() to copy and present, scroll()
    /// for the fast path. A null store (the driver refused it) degrades to painting straight into the
    /// window target, which is then presented by platform_window_t::present().
    ///
    /// @par Lifetime and ordering
    /// Create windows after a gui_application_t (the platform's video side) and destroy them before it;
    /// destroy the gui_application_t before the core_application, because the platform teardown would
    /// otherwise take video away from live windows. Window objects are single-threaded: create, paint
    /// and destroy them on the thread that runs the event loop.
    ///
    /// @par Subclassing
    /// window_t is not final: override paint_event() to draw, resize_event() / move_event() /
    /// expose_event() to react to the queued window events, close_event() to confirm a close, or
    /// key_press_event() to handle keys. The demo examples/hello_gui does exactly this, and the item
    /// tree host (ui::widget_window_t) plugs into the same hooks through its internal adapter window.
    /// A test that needs a different platform injects one with
    /// window_t(std::unique_ptr<platform_window_t>) instead of subclassing the platform side.
    class HWSHQTB_GUI_EXPORT window_t: public paint_device_t, public ::hwshqtb::metaobject::object_t {
        OBJECT

    public:
        /// @brief Create a hidden window of @p size with @p title through the platform factory.
        /// @param title The window title.
        /// @param size The client area in WINDOW coordinates (the platform's window-creation space; on
        ///        a backend whose pixel density is 1 — Windows — that is also the pixel grid the frames
        ///        use); both sides must be positive.
        /// @throws std::invalid_argument when @p size is empty.
        /// @throws std::runtime_error when video is not initialised, or the platform cannot create the
        ///         window/renderer (the message carries the platform's error text).
        /// @note The window starts hidden; call show() to display it and paint the first frame.
        window_t(std::string_view title, extent_t size);

        /// @brief Adopt an externally created platform window (tests and custom platforms).
        /// @param window The platform half; a non-null pointer is attached to this object here.
        /// @throws std::invalid_argument when @p window is null.
        /// @note Nothing is created and no platform factory is consulted: the whole frame policy runs
        ///       over whatever platform implementation is injected, which is how the frame tests drive
        ///       it with no window system at all.
        explicit window_t(std::unique_ptr<platform_window_t> window);

        /// @brief Stop listening to the platform, and destroy the platform half (its engine, its store
        ///        and its native window).
        ~window_t() override;

        window_t(const window_t&) = delete;
        window_t& operator=(const window_t&) = delete;
        window_t(window_t&&) = delete;
        window_t& operator=(window_t&&) = delete;

        /// @brief Show the window and paint the first frame synchronously.
        /// @throws std::runtime_error when the platform cannot show the window.
        void show();

        /// @brief Hide the window (the renderer and the device stay alive).
        /// @throws std::runtime_error when the platform cannot hide the window.
        void hide();

        /// @brief Request a repaint on the next event-loop iteration (QWidget::update).
        ///
        /// It marks a repaint pending and posts one event_type_enum::PAINT event to this thread's
        /// dispatcher. Safe to call repeatedly; only one paint request is outstanding at a time.
        /// Outside a running loop the request simply waits in the dispatcher queue (and is discarded
        /// when the window is destroyed).
        /// @note While the platform owns the frame schedule (its pump is blocked, see
        ///       platform_window_t::defer_repaint()) the request is handed to the platform instead of
        ///       being posted, and its own timer paints the frame.
        void update();

        /// @brief Repaint immediately and present (QWidget::repaint).
        ///
        /// Clears the frame to white, calls paint_event() with a fresh painter and presents. Use it for
        /// the first frame, for a synchronous capture, or in a test that drives the window without a
        /// running event loop; ordinary redraws go through update().
        void repaint();

        /// @brief Ask to close the window: mark close_requested(), hide the window, and quit the
        ///        application's main event loop.
        ///
        /// This is the single-window policy (QGuiApplication::quitOnLastWindowClosed): with one
        /// window, closing it ends the loop. A platform that refuses the hide is ignored on this path:
        /// the window is going away anyway.
        void close();

        /// @brief Whether the window is currently shown.
        /// @note noexcept.
        bool is_visible() const noexcept;

        /// @brief The platform window id this window was created with.
        ///
        /// Every native event that belongs to this window carries it, which is how the platform half
        /// correlates events and how a test tags an injected event with this window.
        /// @return The id, as a plain integer: no platform type leaves this layer.
        /// @note noexcept. Stable for the lifetime of the window.
        std::uint32_t window_id() const noexcept;

#if defined(_WIN32)
        /// @brief The Win32 timer id of the platform's size-move modal-loop frame pump (WM_TIMER
        ///        wParam).
        /// @note Windows only. Exposed so a test can inject the matching WM_TIMER through the window
        ///       handle; the pump itself is installed by the platform half.
        static constexpr unsigned int size_move_timer_id = 0x534D;
#endif

        /// @brief Whether close()/close_event() has been requested.
        /// @note noexcept. Also true when the user pressed the close button or ESC.
        bool close_requested() const noexcept;

        /// @brief Change the window title.
        /// @param title The new title.
        /// @throws std::runtime_error when the platform cannot change it.
        void set_title(std::string_view title);

        /// @brief The smallest client size the window system will let the user shrink this window to.
        /// @param size The floor in window coordinates; a zero component means no floor on that axis.
        /// @note Forwarded to the platform half, which is what the window manager enforces. A backend
        ///       without a resizable window ignores it.
        /// @note noexcept: a refused hint leaves the window without a floor; it is not an error.
        void set_minimum_size(extent_t size) noexcept;

        /// @brief The device size in pixels (the renderer's output size).
        ///
        /// It is queried live, so it already reports a resize whose resize_event_t is still queued,
        /// and it is also correct before the first paint.
        extent_t size() const noexcept override;

        /// @brief The window's paint engine, or nullptr when it has none.
        /// @return The backend the platform created over this window's renderer, or nullptr when there
        ///         is no render target.
        /// @note A pure query: it never creates anything and never throws.
        /// @note noexcept.
        paint_engine_t* paint_engine() noexcept override;

        /// @brief Present the current frame.
        /// @note A failed present is not fatal and is ignored: a minimized or hidden window may refuse
        ///       to present, and the next paint will try again.
        /// @note A painted frame is flushed and presented by paint_frame() itself, so calling this
        ///       after repaint() presents the same window target twice.
        void present();

        /// @brief The damage the host reported for the next frame, in window coordinates.
        /// @return The region the host asked to repaint; empty when nothing was reported since the last
        ///         painted frame. The item tree's root damage() is what the host reports here.
        /// @note This is what the frame paints: one pass per rectangle of the region, each clipped to
        ///       that rectangle and nothing else, and an empty region paints no frame at all (no draw,
        ///       no flush, no present).
        /// @note noexcept.
        const region_t& pending_damage() const noexcept;

        /// @brief Report the damage the host wants repainted on the next frame.
        /// @param damage The whole damage of the host's tree, in window coordinates; an empty region
        ///        reports "nothing damaged".
        /// @note This is the HOST contract, public because the host is no longer a window subclass: the
        ///       item-tree host (ui::widget_window_t) calls it with the root item's damage() when an
        ///       update reaches its repaint sink, then asks for the frame through update(). A test that
        ///       drives the frame policy directly reports its damage the same way.
        /// @note The next painted frame consumes what it painted: paint_frame() subtracts the painted
        ///       rectangles from the region, so damage reported WHILE a frame paints survives into the
        ///       next one, and paints one clipped pass per rectangle of what is left.
        void set_pending_damage(const region_t& damage);

        /// @brief How many frames this window has painted so far.
        /// @return The number of frames that drew and presented; a frame skipped because nothing was
        ///         damaged does not count, and neither does a frame whose painter could not start.
        /// @note This counts FRAMES, not paint_event() calls: a frame made of N damaged rectangles
        ///       paints with one clip per rectangle (N passes), and counts once. A backend that clips
        ///       to a region paints the whole damage in one pass, and still counts once.
        /// @note noexcept.
        std::size_t painted_frame_count() const noexcept;

        /// @brief The region the last painted frame copied from the backing surface to the window
        ///        target: the rectangles whose pixels the frame actually presented.
        ///
        /// The window target is not a persistent surface: a two-buffer flip-model swap chain makes the
        /// buffer a frame renders into hold the pixels of the frame TWO presents ago, while a software
        /// renderer keeps them. A caller that has to know what the window shows — a test modelling the
        /// presented frame, a diagnostic — reads this region: everything outside it holds an older
        /// frame's pixels.
        ///
        /// @return The presented rectangles, in window coordinates; empty before the first painted
        ///         frame.
        /// @note noexcept. A frame that paints nothing presents nothing and leaves this unchanged.
        const region_t& presented_region() const noexcept;

        /// @brief Shift already-painted content inside @p rect by (@p dx, @p dy) and damage only the
        ///        band that the shifts since the last painted frame uncovered
        ///        (QWidget::scroll / QBackingStore::scroll).
        ///
        /// The scroll fast path of a container: instead of repainting the whole area, the pixels that
        /// are already in the backing surface move and only the band the shift uncovers is marked for
        /// repaint. A scroll by at least the extent, or a window without a backing surface, degrades to
        /// "damage all of @p rect", which is always correct.
        ///
        /// @param rect The scrolled area, in window coordinates; it is clipped to the client area.
        /// @param dx The horizontal shift: a pixel at x() ends up at x() + @p dx.
        /// @param dy The vertical shift: a pixel at y() ends up at y() + @p dy.
        /// @return The region this call added to pending_damage(), in window coordinates; empty when
        ///         the call did nothing. A host that records the same damage in its own tree calls this
        ///         and damages exactly the returned region there, which keeps the tree and the surface
        ///         in agreement.
        ///
        /// @par Several shifts before one painted frame
        /// A frame is painted later than the input that caused the shifts, so a fast scroll arrives
        /// here several times before anything is repainted. The damage of one step is NOT enough in
        /// that case: the next shift moves the band the previous one exposed further into the area, so
        /// the stale band grows by one shift per step. The window therefore accumulates the shifts of
        /// every rectangle since the last painted frame and damages the band of the ACCUMULATED shift —
        /// the exact band no content from the last painted frame can cover. A step that reverses an
        /// axis, a rectangle that overlaps another one shifted since that frame, and a refused shift all
        /// degrade to "damage all of @p rect", which is always correct.
        ///
        /// @par Pending damage moves with the pixels
        /// Damage describes pixels, so the damage already pending inside @p rect moves with them: its
        /// part inside the rectangle is translated by (@p dx, @p dy) and clipped back into it, and the
        /// band is united only AFTER that. Without the translation a record made between two shifts (a
        /// hover band the second step drags along) would repaint the place its pixels LEFT and the stale
        /// band would stay on screen.
        ///
        /// @note The exposed regions are united into pending_damage() and a repaint is scheduled, so the
        ///       next frame paints exactly them. A caller that also records damage in an item tree must
        ///       damage the SAME region there — the returned region answers it.
        /// @note Call it OUTSIDE a paint pass. Inside one the surface is the current render target and
        ///       cannot be its own source, so the shift degrades to damaging all of @p rect.
        region_t scroll(::hwshqtb::gui::rect_t rect, int dx, int dy);

        /// @brief The region a scroll of @p rect by (@p dx, @p dy) uncovers.
        /// @param rect The scrolled area, in window coordinates.
        /// @param dx The horizontal shift, in pixels.
        /// @param dy The vertical shift, in pixels.
        /// @return The part of @p rect that no shifted content covers: one vertical strip, one
        ///         horizontal strip, both, or the whole of @p rect when a shift is at least the
        ///         corresponding extent.
        /// @note Header-only and side-effect free: the window uses it for the region of scroll(), and a
        ///       host that keeps its own damage bookkeeping uses it too, so both always agree.
        /// @note Pass the ACCUMULATED shift of every step since the last painted frame, never the shift
        ///       of a single step, or the band the earlier steps exposed stays stale.
        static region_t scroll_exposed_region(::hwshqtb::gui::rect_t rect, int dx, int dy) {
            region_t exposed;
            if (rect.isEmpty()) {
                return exposed;
            }
            const int width = rect.width();
            const int height = rect.height();
            const int horizontal = dx < 0 ? -dx : dx;
            const int vertical = dy < 0 ? -dy : dy;
            if (horizontal >= width || vertical >= height) {
                exposed.unite(rect); // the shift keeps nothing inside the rectangle
                return exposed;
            }
            // The band the content left behind: the vertical one spans the full height of the area,
            // the horizontal one the full width, and their union is the L the caller must repaint.
            if (dx > 0) {
                exposed.unite(::hwshqtb::gui::rect_t{ rect.x(), rect.y(), dx, height });
            }
            else if (dx < 0) {
                exposed.unite(::hwshqtb::gui::rect_t{ rect.right() + dx, rect.y(), -dx, height });
            }
            if (dy > 0) {
                exposed.unite(::hwshqtb::gui::rect_t{ rect.x(), rect.y(), width, dy });
            }
            else if (dy < 0) {
                exposed.unite(::hwshqtb::gui::rect_t{ rect.x(), rect.bottom() + dy, width, -dy });
            }
            return exposed;
        }

        /// @brief Render one frame and write it to @p path as a BMP, then present.
        ///
        /// The capture path for tests and headless inspection: it paints exactly one frame (clear +
        /// paint_event with a fresh painter, so the output is deterministic for a frame counter that
        /// starts at 0) into the store's surface, reads that surface back and writes the pixels. It
        /// needs neither a shown window nor a running event loop.
        /// When the driver refused the store, the frame is painted straight into the window target and
        /// read BEFORE presenting, because a flip-model swapchain's back buffer is undefined after
        /// present. The platform normalises the pixels to 24-bit BGR first, so the file is byte-identical
        /// whichever path produced the frame (windowed, dummy or offscreen).
        ///
        /// @param path The output file path.
        /// @throws std::runtime_error when the pixels cannot be read back or the file cannot be written
        ///         (the message carries the platform's error text).
        void save_screenshot(std::string_view path);

        /// @brief Write the frame the last paint left in the store's surface to @p path as a BMP,
        ///        WITHOUT painting or presenting anything.
        ///
        /// This is the diagnostic capture of the PRESENTED frame: paint_frame() flushes the whole
        /// surface to the window target on every painted frame, so the surface content is exactly what
        /// the window shows. Unlike save_screenshot() it does not run a full repaint, which would repair
        /// any stale pixels and hide the very defect a capture is meant to show. The store is made the
        /// read target for the read and the window target is restored afterwards.
        ///
        /// @param path The output file path.
        /// @throws std::runtime_error when the pixels cannot be read back or the file cannot be written
        ///         (the message carries the platform's error text).
        /// @note The window must have painted at least one frame; without a store the window target is
        ///       read instead, and a flip-model back buffer is undefined after a present, so that
        ///       fallback cannot be relied on.
        /// @note DIAGNOSTIC USE: this is the capture a defect reproduction must use. save_screenshot()
        ///       repaints the whole frame before reading it, which repairs the very stale pixels (the
        ///       stale band, the double buffer, the un-scrolled frame) a reproduction is trying to show,
        ///       so it cannot reproduce them.
        /// @note Does not touch pending_damage(), presented_region() or painted_frame_count().
        void save_painted_frame(std::string_view path);

        /// @brief This window's input state (I1): the mouse position, held buttons/keys, modifiers and
        ///        the per-frame press/release/wheel edges.
        ///
        /// The window maintains it from the platform's reports, which already belong to this window, so
        /// the state is a faithful snapshot of what happened since the previous painted frame:
        ///  - levels: pointer motion, button down/up, key down/up;
        ///  - edges: the button and key reports set pressed()/released() for the current frame;
        ///  - wheel: wheel reports accumulate into wheel_delta();
        ///  - modifiers: the modifier set the platform reports with each input step.
        /// window_t clears the per-frame data (both edges and the wheel delta) with
        /// input_state_t::reset_frame() immediately after paint_event() returns — i.e. at the end of the
        /// frame that consumed them. That is the documented one-frame edge lifetime; see
        /// input_state_t.h.
        ///
        /// @return The state; the non-const form exists for this window's own reports — application
        ///         painting code reads the const form.
        /// @note The window is single-threaded, and so is its input state: read it on the thread that
        ///       runs the event loop.
        input_state_t& input_state() noexcept;

        /// @brief This window's input state (read-only).
        const input_state_t& input_state() const noexcept;

        /// @name Platform implementation entries
        /// Called by the platform half (or by a test that injects one). They are public because the
        /// platform is a separate object, not a subclass; nothing above the GUI layer calls them.
        ///
        /// Input is ONE entry, not one per event kind: the platform fills in a single interaction_t
        /// step and hands it over, and this class derives everything else from it and from its own
        /// input_state — the shape QWindowSystemInterface has, where one event object carries the whole
        /// payload and no per-kind method exists on the window.
        /// @{

        /// @brief The platform reports that the device size may have changed.
        /// @param deliver_now false to queue a resize_event_t (the normal, event-loop path); true to
        ///        deliver it synchronously — the platform's modal loop has no event pump to dispatch
        ///        through. The synchronous form also asks for a repaint, so a platform that owns the
        ///        frame schedule (defer_repaint()) keeps its repair frame dirty without an extra query.
        /// @note Compares against the size last reported through resize_event() and reports once per
        ///       change, so a backend that reports the same size twice never causes a duplicate layout;
        ///       a size already delivered during a drag is not reported again when the pump resumes.
        void handle_resize(bool deliver_now = false);

        /// @brief The platform reports that the window moved.
        /// @param global_x The new x position in global screen coordinates.
        /// @param global_y The new y position in global screen coordinates.
        void handle_move(int global_x, int global_y);

        /// @brief The platform reports that the window's contents may have been lost.
        /// @note Damages the whole client area and schedules the frame HERE, then queues the
        ///       expose_event_t that runs expose_event(): a platform's modal frame pump cannot wait for
        ///       a dispatch, and the frame it asks for is the one that pumps a drag.
        void handle_expose();

        /// @brief The platform reports a close request (close button, Alt+F4, system gesture).
        void handle_close();

        /// @brief The platform reports that this window gained (@p gained true) or lost the keyboard
        ///        focus.
        /// @note A gain starts the platform text input: a backend produces no committed text until it is
        ///       on. A loss stops it, drops the held-key set (no key-up arrives while another window owns
        ///       the keyboard, so that set is stale) and calls focus_lost_event().
        void handle_focus(bool gained);

        /// @brief The platform reports one input step.
        /// @param step The step, with interaction_t::mouse in WINDOW coordinates (the platform's own
        ///        space) — this class converts it to the pixel space exactly once, through
        ///        pixel_point_of(), because the pixel density is a window property.
        ///
        /// @par What the step may carry
        /// Exactly one kind of payload per call, identified by which fields are set:
        ///  - wheel != {0, 0}                     — wheel ticks;
        ///  - text_input non-empty               — committed text (UTF-8);
        ///  - key != 0 with key_pressed/_released — a key edge;
        ///  - mouse_pressed / mouse_released     — a button edge (interaction_t::button names it);
        ///  - none of the above                  — pointer motion, with interaction_t::buttons as the
        ///                                         held-button LEVEL.
        ///
        /// @par What this class does
        /// It maintains input_state_t from the step: the position (always), the modifiers (always), the
        /// wheel accumulator, the committed text, the held keys and the button level/edges — then calls
        /// interact_event() with the converted step, then clears the wheel and text accumulators (their
        /// payload belongs to the step that carried it), then calls update().
        /// @note A button step is routed to interact_event() for the LEFT button only (the one the item
        ///       tree routes); its level and edges are recorded for every button.
        /// @note A press step calls key_press_event(key) BEFORE the step reaches interact_event(), so a
        ///       window that closes on ESC (examples/hello_gui) does so in the same order as before.
        void handle_input(const interaction_t& step);

        /// @brief The platform reports that the pointer left the client area.
        /// @note The pointer session of this window has ended, so no later step can clear what the last
        ///       one set: calls pointer_leave_event(), which is where a host drops its pointer state.
        void handle_pointer_leave();

        /// @brief Paint one frame immediately, WITHOUT consuming the input edges.
        /// @note The platform's size-move repair frame: its input events are still queued and have not
        ///       been dispatched, so clearing the edges here would drop them.
        void repair_frame();
        /// @}

    protected:
        /// @brief The client area in WINDOW coordinates: the space the platform reports pointer
        ///        positions in.
        ///
        /// Together with pixel_size() it defines the pixel density the pointer conversion applies:
        /// pixel_point_of() multiplies the point by pixel_size() / window_coordinate_size() per axis.
        /// The default forwards to the platform half.
        ///
        /// @return The client area's extent in window coordinates; empty when the window reports none
        ///         (the conversion is then the identity).
        /// @note Override this AND pixel_size() with a consistent pair to simulate a pixel density the
        ///       driver does not report; overriding only one maps the pointer onto a grid the frame does
        ///       not use.
        virtual extent_t window_coordinate_size() const noexcept;

        /// @brief The client area in PIXELS: the pixel grid the item tree, damage and painting live in.
        /// @return The client area's extent in pixels; empty when the window reports none.
        /// @note The second half of the density pair; see window_coordinate_size().
        /// @note The default forwards to the platform half. The FRAME's own client area (the damage
        ///       clip and the presented region) is read from the platform directly, so an override here
        ///       only changes the pointer conversion, never the pixels a frame touches.
        virtual extent_t pixel_size() const noexcept;

        /// @brief Draw the damaged area of a frame; the area is already cleared to white and the
        ///        painter is fresh.
        ///
        /// The default does nothing (an empty white window). Override it to draw. The painter's state is
        /// the documented default — opaque black colour, no clip, identity transform — and is
        /// re-established on every frame, so an override must set the colour it wants.
        ///
        /// @param event The paint event: event->painter() is this frame's painter (it belongs to the
        ///        caller and must not be stored) and event->damage() the area this pass owns.
        /// @note Called at least once per painted frame, with the painter clipped to the damaged area
        ///       and that area already cleared to white. The frame decides how many passes that is: one
        ///       pass per damaged rectangle for a backend whose clip is a single rectangle, or ONE pass
        ///       with the whole region as the clip for a backend that declares
        ///       engine_feature_flag::CLIP_REGION. In both cases painting outside the clip is dropped by
        ///       the backend, so an override may draw as if the whole window were dirty; it must not
        ///       ASSUME that everything it draws is visible, because the pixels outside the clip keep
        ///       their previous contents (that is what makes a partial repaint cheap).
        /// @note A frame with no damage does not call this at all.
        virtual void paint_event(paint_event_t* event);

        /// @brief React to a size change (QWidget::resizeEvent).
        /// @param event The queued resize event; event->size() is the latest size in PIXELS — the same
        ///        space as size() — and event->old_size() the size before the first pending change.
        /// @note The renderer has already followed the new size, and a repaint is scheduled after this
        ///       call, so the new size is painted even when the override does nothing.
        virtual void resize_event(resize_event_t* event);

        /// @brief React to a move (QWidget::moveEvent).
        /// @param event The queued move event, in global screen coordinates.
        /// @note A move does not schedule a repaint: the client area did not change.
        virtual void move_event(move_event_t* event);

        /// @brief React to an expose notification (QWindow::exposeEvent).
        /// @param event The queued expose event; it carries no payload.
        /// @note A repaint is scheduled after this call, because an exposed surface may have lost its
        ///       contents.
        virtual void expose_event(expose_event_t* event);

        /// @brief React to a close request (window close button, Alt+F4, or close()).
        /// @param event The close event; it carries no payload.
        ///
        /// The default calls close(), which marks close_requested() and quits the main loop. Override it
        /// to ask for confirmation; call close() yourself when the close should happen.
        virtual void close_event(close_event_t* event);

        /// @brief Handle a key press in this window.
        /// @param key The platform key code (27 is ESC, 65 'A', ...).
        /// @return true when the event was handled; the return value is advisory (no other consumer sees
        ///         a key the window already owns).
        /// @note The default closes the window on ESC and ignores every other key.
        virtual bool key_press_event(int key);

        /// @brief React to one input step the platform reported (hover, press, release, wheel, key,
        ///        text).
        /// @param event The step event; step() is already carrying the pixel-space pointer position and
        ///        the modifier bits, and its edges (mouse_pressed/mouse_released, wheel, text_input)
        ///        belong to this step only.
        /// @note The default does nothing. The item-tree host uses it to route the step into its
        ///       widget tree; the input state itself is maintained by window_t BEFORE the call, so an
        ///       override may read input_state() directly.
        /// @note After the call window_t clears the wheel and text accumulators this step delivered, so
        ///       one notch is seen by exactly one step.
        virtual void interact_event(interact_event_t* event);

        /// @brief React to the keyboard focus leaving this window (see handle_focus(false)).
        /// @param event The focus-out event; it carries no payload.
        /// @note The default does nothing. The item-tree host drops its pointer session here.
        virtual void focus_out_event(focus_out_event_t* event);

        /// @brief React to the pointer leaving the client area (QWidget::leaveEvent equivalent).
        /// @param event The leave event; it carries no payload.
        /// @note The default does nothing. The item-tree host drops its pointer session here, exactly
        ///       like it does on a focus loss: the two ends of the same session.
        /// @note Sent for every client-area exit; the input state itself is already updated.
        virtual void leave_event(leave_event_t* event);

    private:
        /// @brief One rectangle shifted since the last painted frame: the accumulated shift and the
        ///        whole-rectangle fallback flag.
        struct scroll_record_t {
            /// @brief The shifted rectangle, already clipped to the client area.
            rect_t rect;
            /// @brief The horizontal shift accumulated since the last painted frame.
            int dx = 0;
            /// @brief The vertical shift accumulated since the last painted frame.
            int dy = 0;
            /// @brief Whether only a whole-rectangle repaint is correct (a reversed axis, an
            ///        overlapping rectangle, or a shift the surface refused).
            bool whole = false;
        };

        /// @brief Receive one queued window event and dispatch it to the matching virtual hook.
        /// @param event The event; PAINT / RESIZE / MOVE / EXPOSE are consumed here.
        /// @return true when the event belonged to this window.
        /// @note This is the object_t event ENTRY (the gui layer's only one), so a window is now a
        ///       real object in the event system: the dispatcher posts to the window itself instead of
        ///       to the internal pump object that used to sit beside it.
        /// @note The event type is the only discriminator: window_t is the only producer of these four
        ///       types, so the down-casts are sound.
        bool event(::hwshqtb::metaobject::event_t* event) override;

        /// @brief Paint a pending frame (called when the queued request is dispatched).
        void paint_if_pending();

        /// @brief Damage the whole client area.
        void damage_all();

        /// @brief The client area as a rectangle in the pixel space: the grid one frame works on.
        /// @return {0, 0, platform pixel size}: the rectangle a damage region is clipped to, a scroll
        ///         shifts, and a painted frame copies to the window.
        rect_t client_area_pixels() const noexcept;

        /// @brief The whole window as one region (the flush area of a full frame).
        /// @return {0, 0, pixel size} as one rectangle, in the pixel space; an empty region when the
        ///         window reports no client area.
        region_t full_window_region() const;

        /// @brief A platform pointer position in this window's pixel space.
        /// @param window_point The pointer, in WINDOW coordinates (the platform's own space).
        /// @return The point in the space of size(), which is the space geometry, damage and hit
        ///         testing use.
        /// @note The ONE place a pointer position crosses from the platform's window coordinates into
        ///       the paint space: every pointer report goes through it. The extents it divides are the
        ///       two virtuals, so a test can inject a density this display does not have; at a density
        ///       of 1 the conversion is the identity.
        point_t pixel_point_of(point_t window_point) const noexcept;

        /// @brief The persistent surface this window paints into, or nullptr when the driver refused it.
        /// @return The platform's store; not owned by this class and outlived by it.
        backing_store_t* surface_store() const noexcept;

        /// @brief Remember the platform's current geometry: the size a resize report compares against,
        ///        and whether the window has a screen position at all.
        /// @note An EMPTY geometry (a non-positive client-area size) means the platform's window is not
        ///       placed on any screen — an offscreen target, a headless backend. There is then no
        ///       position to remember, no move can be reported, and the pointer conversion has no
        ///       window-coordinate extent (so it is the identity). rect_t's own emptiness carries that
        ///       state; no sentinel value and no second query exist for it.
        /// @note Called from both constructors and from show(), which is what keeps the resize
        ///       bookkeeping consistent with what the platform really reports.
        /// @note noexcept.
        void seed_geometry() noexcept;

        /// @brief Draw one frame into the backing store, or into the window target when the store is
        ///        unavailable, and leave the used target current.
        /// @param consume_input true to clear the per-frame input edges after the frame (the normal
        ///        path); false for a platform's size-move pump, whose edges have not been dispatched and
        ///        must survive the repair frame.
        /// @param damage The region to paint, in window coordinates; an empty region paints no
        ///        rectangle (the background fill is skipped, nothing is drawn).
        /// @return true when the frame is in the store's surface and it is still the render target;
        ///         false when the frame went straight into the window target (no store, or the painter
        ///         could not start).
        /// @note The store is resized to the client area and made the render target here; closing the
        ///       pass is the caller's business (see the note below).
        /// @note The pass count is this function's decision: one pass per damaged rectangle, or one pass
        ///       for the whole region when the backend declares engine_feature_flag::CLIP_REGION (see
        ///       paint_event()).
        /// @note The caller owns the end of the pass: paint_frame() calls end_paint() and flushes, and
        ///         save_screenshot() reads the target before that because a window back buffer is
        ///         undefined after present.
        bool draw_frame(bool consume_input, const region_t& damage);

        /// @brief Clear the frame, call paint_event() for the damaged area, flush the store to the
        ///        window and present.
        /// @param consume_input true to clear the per-frame input edges after the frame (the normal
        ///        path); false for a platform's size-move repair frame, whose edges have not been
        ///        dispatched and must survive it.
        /// @note An empty pending_damage() paints nothing at all: no background fill, no paint_event(),
        ///       no flush and no present. That is what makes a window at rest cost no GPU work.
        void paint_frame(bool consume_input);

        /// @brief The platform half; never null in a constructed window.
        std::unique_ptr<platform_window_t> _platform;
        /// @brief The input state of this window (I1), maintained by the reports above.
        input_state_t _input;
        /// @brief The damage the host reported for the next frame, in window coordinates.
        region_t _pending_damage;
        /// @brief The region the last painted frame presented to the window target.
        region_t _presented;
        /// @brief The rectangles shifted since the last painted frame; a painted frame clears it.
        /// @note The damage of a scroll step alone is not enough when several steps coalesce into one
        ///       frame: the following shift moves the band the step exposed further into the area, so
        ///       the band has to be recomputed from the accumulated shift (see scroll()).
        std::vector<scroll_record_t> _scrolls;
        /// @brief How many frames this window has painted (see painted_frame_count()).
        std::size_t _painted_frames = 0;
        /// @brief Whether update() has posted a paint request the loop has not dispatched yet.
        bool _paint_pending = false;
        /// @brief Whether close()/close_event() already asked for the window to close.
        bool _close_requested = false;
        /// @brief The device size at the last resize we reported, in PIXELS.
        /// @note One copy, shared by the queued path (handle_resize()) and the platform's modal pump
        ///       (handle_resize(true)), so the two can never disagree and a resize handled during a drag
        ///       is not reported again when the pump resumes.
        extent_t _last_size;
        /// @brief The window position at the last move we reported (global screen coordinates).
        /// @note Only meaningful while @c _has_position is true.
        point_t _last_position;
        /// @brief Whether the platform has placed the window on a screen, i.e. whether
        ///        _last_position means anything.
        /// @note False when the platform's window_geometry() is EMPTY (no position, no client area in
        ///       window coordinates). A window that has never been placed does not MOVE: the first
        ///       report it gives becomes its position without a move event, because a move event would
        ///       have to name an old position that does not exist.
        bool _has_position = false;
    };
}

#endif