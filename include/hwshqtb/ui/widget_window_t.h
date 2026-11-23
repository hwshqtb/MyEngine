#ifndef HWSHQTB__UI__WIDGET_WINDOW_T_H
#define HWSHQTB__UI__WIDGET_WINDOW_T_H

/// @file widget_window_t.h
/// @brief widget_window_t: the retained host of the widget layer — a COMPOSED host that paints a
///        widget tree into a gui::window_t and routes the window's input into it.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/input_state_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/gui/window_t.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/input_router_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    class widget_t;

    /// @brief A host that owns one root widget, paints its whole tree into a window and routes input.
    ///
    /// @par Composition, not inheritance
    /// This class is NOT a gui::window_t: it HOLDS one. The window keeps the native resources, the
    /// frame policy and the input state; the host owns the tree, the input router and the two item
    /// sinks. That is the Qt QWidgetWindow shape: a QWidgetWindow is the object that owns a widget
    /// tree and drives a QWindow, not a kind of QWindow.
    ///
    /// The consequence is the point of the split: gui::window_t is usable, complete and subclassable
    /// WITHOUT this layer (examples/hello_gui paints, receives input and captures with nothing from
    /// ui), and nothing here names a platform type.
    ///
    /// @par Two constructors
    ///  - widget_window_t(title, size) creates its own window: an internal adapter window
    ///    (detail::tree_window_t) forwards paint/resize/input to this host, so the host is a
    ///    self-contained top-level widget window.
    ///  - widget_window_t(window) attaches to a window the caller owns: the caller's window subclass
    ///    forwards its own hooks to paint() / resized() / interact() / focus_lost() / pointer_left().
    ///    That is how a test drives a tree over its own instrumented window.
    ///
    /// @par Repaint path
    /// The host installs the repaint sink on the root itself, so widget_t::update() from ANY widget of
    /// the tree reaches this host: the sink publishes the root's damage() — the union of every item's
    /// damage, recorded up each ancestor chain — as window_t::pending_damage() and schedules the frame
    /// through window_t::update(). Repaints are still coalesced and event-loop driven by the window:
    /// the widget layer never paints by itself.
    ///
    /// @par Scroll path
    /// The host also installs the scroll sink on the root. A container that can move already-painted
    /// pixels (ui::scroll_view_t) asks for it through widget_t::notify_scroll(), which walks up to that
    /// sink exactly like update() does; the host shifts the window's backing surface
    /// (window_t::scroll()), damages the strips the shift uncovers in the tree and publishes the same
    /// region as pending damage.
    ///
    /// @par Three landing points
    /// The window reports a frame and an input step through five plain member functions: paint(),
    /// resized(), interact(), focus_lost() and pointer_left(). They are public because the forwarding
    /// window is a separate object (the internal adapter, or the caller's own window subclass); they
    /// are not virtual, because there is no second host to dispatch on. A window subclass that
    /// attaches to this host forwards its hooks here and is done.
    ///
    /// @par Who clears the pointer state
    /// The router does, and only the router: hover/press/capture are ITS input session's state, and
    /// this host only tells it when the session ends. A window that lost the mouse (the pointer left
    /// the client area) or the focus can receive no further pointer steps, so focus_lost() and
    /// pointer_left() call input_router_t::clear_pointer_state() and republish the damage that clearing
    /// recorded. A view never clears hover itself.
    ///
    /// The router is also where both item sinks are installed: it puts the focus sink and the pointer
    /// sink on the root when this host creates it, so an item's set_focus() / capture_pointer() reaches
    /// the router from anywhere in the tree.
    ///
    /// @par Sizing
    /// The root is laid out to fill the client area: set_root_widget() sets its geometry to the window
    /// size, and resized() repeats that on every resize (which also triggers update()). resized() also
    /// drops a focus that became invisible, so a hidden focused widget cannot keep receiving keys.
    ///
    /// @par Ownership
    /// set_root_widget() takes ownership: the host deletes the root (and, through the object tree, its
    /// widget subtree) after destroying the router built on it and detaching the view's sinks. A widget
    /// that already has a parent is detached from it first, so adopting a widget that lives under the
    /// previous root is safe. Passing nullptr (or destroying the host) drops the current tree. The
    /// tree is destroyed BEFORE a window this host created, so no callback can reach a dead host.
    ///
    /// @par Window API
    /// The forwarding functions (show/hide/update/repaint/close/size/...) exist so a call site that
    /// used this class as its window needs no change; window() is the escape hatch to anything else.
    class HWSHQTB_UI_EXPORT widget_window_t: public ::hwshqtb::metaobject::object_t {
    public:
        /// @brief Create a host over its own window with @p title and @p size.
        /// @param title The window title.
        /// @param size The client area, in the window coordinates a window's constructor takes. The
        ///        root widget is then laid out to the DEVICE size (window_t::size()), which is the pixel
        ///        grid the tree paints and hit-tests in; the two only differ on a backend whose pixel
        ///        density is above 1.
        /// @throws std::invalid_argument when @p size is empty (from the window's constructor).
        /// @throws std::runtime_error when the platform cannot create the window.
        widget_window_t(std::string_view title, ::hwshqtb::gui::extent_t size);

        /// @brief Attach to a window the caller owns.
        /// @param window The window; the caller keeps ownership and must outlive this host. The caller's
        ///        window subclass is responsible for forwarding paint_event() / resize_event() /
        ///        interact_event() / focus_lost_event() / pointer_leave_event() to the matching landing
        ///        points below.
        explicit widget_window_t(::hwshqtb::gui::window_t& window);

        /// @brief Destroy the router and the root widget tree, then the window this host created (if
        ///        any).
        ~widget_window_t();

        widget_window_t(const widget_window_t&) = delete;
        widget_window_t& operator=(const widget_window_t&) = delete;
        widget_window_t(widget_window_t&&) = delete;
        widget_window_t& operator=(widget_window_t&&) = delete;

        /// @brief The root widget, or nullptr when none is set.
        widget_t* root_widget() const;

        /// @brief Adopt @p widget as the root of this host (deletes the previous root).
        /// @param widget The new root; nullptr removes the current tree. Its geometry is set to the
        ///        window size, it receives the repaint and scroll sinks, and a fresh input_router_t is
        ///        built on it.
        void set_root_widget(widget_t* widget);

        /// @brief The router of the current root, or nullptr when no root is adopted.
        /// @note The router is the focus authority, so this is how a host focuses a widget
        ///       programmatically without going through item_t::set_focus().
        input_router_t* router() const;

        /// @brief Paint the root tree over the area the window handed this pass.
        /// @param event The pass the window received; the window has already clipped the painter to the
        ///        damaged area and cleared that area.
        /// @note The area is `event->damage()`. The event is forwarded to the tree UNCHANGED — the gui
        ///       layer hands a `paint_event_t*` and the tree consumes the same type, so this host adds no
        ///       wrapper, no pass logic, no clear and no engine query.
        void paint_event(paint_event_t* event);

        /// @brief Paint the tree into @p painter (the composition entry: no window needed).
        /// @param painter The painter of this pass; its clip is the area to cover.
        void render(::hwshqtb::gui::painter_t& painter);

        /// @brief Resize the root to fill the new client area and drop a focus that became invisible.
        /// @param event The queued resize event; event.size() is the new client-area extent.
        void resized(const ::hwshqtb::gui::resize_event_t& event);

        /// @brief Route one input step the window reported into the tree.
        /// @param event The step, already carrying the pixel-space pointer position and the modifier
        ///        bits; its edges belong to this step only.
        /// @note The window has already written the step into its input state and schedules the repaint
        ///       after this call, so the host only has to route it.
        void interact(const ::hwshqtb::gui::interaction_t& event);

        /// @brief The keyboard focus left the window: drop the pointer session and republish the damage
        ///        that dropping recorded.
        void focus_lost();

        /// @brief The pointer left the client area: drop the pointer session and republish the damage
        ///        that dropping recorded.
        void pointer_left();

        /// @brief Receive one host request from the tree and dispatch it (the object_t event function).
        /// @param event The event; only PAINT and scroll_event_t are consumed here.
        /// @return true when the event belonged to this host.
        /// @note The tree top holds an object_t* to this host (item_t::set_request_receiver()), so a
        ///       request arrives as an ordinary event — a plain PAINT for "a frame is due" and a
        ///       scroll_event_t for "these pixels moved". This is the ONLY channel between the tree and
        ///       the outside world; the sink callables it replaces were a third mechanism.
        /// @note Sent synchronously (core_application::send_event), so the frame request and the pixel
        ///       move happen in the caller's step, exactly as the sink did.
        /// @note Overridable like every other landing point: a subclass observes the request and calls
        ///       this base to keep the host working.
        bool event(::hwshqtb::metaobject::event_t* event) override;

        /// @brief Show the window and paint its first frame (window_t::show()).
        void show();

        /// @brief Hide the window (window_t::hide()).
        void hide();

        /// @brief Request a repaint on the next event-loop iteration (window_t::update()).
        void update();

        /// @brief Repaint immediately and present (window_t::repaint()).
        void repaint();

        /// @brief Present the current frame (window_t::present()).
        void present();

        /// @brief Ask to close the window (window_t::close()).
        void close();

        /// @brief Whether the window is currently shown.
        /// @note noexcept.
        bool is_visible() const noexcept;

        /// @brief Whether a close has been requested.
        /// @note noexcept.
        bool close_requested() const noexcept;

        /// @brief The platform window id (window_t::window_id()).
        /// @note noexcept.
        std::uint32_t window_id() const noexcept;

        /// @brief Change the window title (window_t::set_title()).
        void set_title(std::string_view title);

        /// @brief The device size in pixels (window_t::size()).
        /// @note noexcept.
        ::hwshqtb::gui::extent_t size() const noexcept;

        /// @brief The damage the next frame paints (window_t::pending_damage()).
        /// @note noexcept.
        const ::hwshqtb::gui::region_t& pending_damage() const noexcept;

        /// @brief The region the last painted frame presented (window_t::presented_region()).
        /// @note noexcept.
        const ::hwshqtb::gui::region_t& presented_region() const noexcept;

        /// @brief How many frames the window has painted (window_t::painted_frame_count()).
        /// @note noexcept.
        std::size_t painted_frame_count() const noexcept;

        /// @brief Shift painted content and damage the uncovered band (window_t::scroll()).
        /// @return The region the call added to pending_damage().
        ::hwshqtb::gui::region_t scroll(::hwshqtb::gui::rect_t rect, int dx, int dy);

        /// @brief Render one frame to @p path as a BMP (window_t::save_screenshot()).
        void save_screenshot(std::string_view path);

        /// @brief Write the presented frame to @p path as a BMP (window_t::save_painted_frame()).
        void save_painted_frame(std::string_view path);

        /// @brief This window's input state (window_t::input_state()).
        ::hwshqtb::gui::input_state_t& input_state() noexcept;

        /// @brief This window's input state, read-only.
        const ::hwshqtb::gui::input_state_t& input_state() const noexcept;

        /// @brief The window this host composes.
        /// @note The escape hatch for anything the forwarding functions do not cover (a subclass hook, a
        ///       platform query, an injected platform window).
        ::hwshqtb::gui::window_t& window() noexcept;

        /// @brief The window this host composes, read-only.
        const ::hwshqtb::gui::window_t& window() const noexcept;

    private:
        /// @brief Push the tree's minimum size onto the window, so the user cannot shrink below it.
        /// @note The floor is the ROOT's minimum_size_hint(): a container derives it from its item
        ///       children, so a label that cannot fit its text stops the whole window from being
        ///       squeezed into overlapping text.
        /// @note Called when the tree is adopted and on every frame request — a size-constraint change
        ///       asks for a frame through update(), so that is the natural refresh point.
        void apply_minimum_size();

        /// @brief Destroy the router and then the tree it routed into.
        void destroy_tree();

        /// @brief The window this host composes; never null.
        ::hwshqtb::gui::window_t* _window = nullptr;
        /// @brief Whether this host created (and therefore owns and destroys) the window.
        bool _owned = false;
        /// @brief The window this host created, when _owned; destroyed after the tree.
        std::unique_ptr<::hwshqtb::gui::window_t> _owned_window;
        /// @brief The owned root widget; may be null.
        std::unique_ptr<widget_t> _root;
        /// @brief The last floor pushed to the window, so an unchanged one is not re-sent.
        ::hwshqtb::gui::extent_t _last_minimum_size{};
        /// @brief The router of @p _root; rebuilt by set_root_widget() (destroyed BEFORE the root).
        std::unique_ptr<input_router_t> _router;
    };
}

#endif
