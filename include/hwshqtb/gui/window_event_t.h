#ifndef HWSHQTB__GUI__WINDOW_EVENT_H
#define HWSHQTB__GUI__WINDOW_EVENT_H

/// @file window_event_t.h
/// @brief The queued window events resize_event_t, move_event_t and expose_event_t
///        (the QResizeEvent / QMoveEvent / QExposeEvent family).

#include "hwshqtb/gui/export.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/region_t.h"
#include "hwshqtb/metaobject/event_t.h"

namespace hwshqtb::gui {
    class painter_t;
    struct interaction_t;

    /// @brief A paint pass is due: draw @c damage through @c painter (QPaintEvent equivalent).
    ///
    /// @par One type for both layers
    /// This is THE paint event. It lives in the gui layer because its payload is a `painter_t` and a
    /// region, and both belong to this layer; the ui layer REUSES it for its item passes (it inherits
    /// `ui::ui_event_t` there, which refines the type code to UI_PAINT). There is deliberately no
    /// second, per-layer paint event: a host that composes a window AND paints items would have to
    /// disambiguate two identically purposed types.
    ///
    /// @par Coordinate space
    /// The region is in the coordinate space of the DEVICE the painter was built for: window pixels
    /// for a window pass, and the item's own coordinates once the ui layer has translated the painter.
    ///
    /// @par Lifetime
    /// Built on the caller's stack, dispatched synchronously while the pass runs, destroyed when it
    /// returns. The painter reference is only valid for that pass.
    class HWSHQTB_GUI_EXPORT paint_event_t final: public ::hwshqtb::metaobject::event_t {
    public:
        /// @brief Construct for @p painter and the area this pass owns.
        /// @param painter The painter, already clipped to @p damage and cleared over it.
        /// @param damage The area of this pass: never empty, and in the painter's space.
        /// @note noexcept: the region is moved in.
        paint_event_t(painter_t& painter, ::hwshqtb::gui::region_t damage) noexcept;

        /// @brief The painter of the frame being produced.
        /// @return The very painter this pass paints with; the const event still allows drawing
        ///         through it, because producing the frame IS the purpose of the event.
        /// @note noexcept.
        painter_t& painter() const noexcept;

        /// @brief The area this pass owns.
        /// @return The region given at construction, in the painter's space.
        /// @note noexcept. A receiver may skip work outside it; the painter is already clipped to it.
        /// @note Ignoring it is always correct (the painter's clip already restricts the drawing).
        const ::hwshqtb::gui::region_t& damage() const noexcept;

    private:
        /// @brief The frame's painter; a reference, because the pass owns it.
        painter_t& _painter;
        /// @brief The area of this pass.
        ::hwshqtb::gui::region_t _damage;
    };

    /// @brief One input step reached the window (the payload the window converts and forwards).
    ///
    /// @par Why a reference, not a copy
    /// `interaction_t` is a value type (an input SNAPSHOT, not an event of its own), so this event
    /// carries a reference to the window's converted step rather than owning a copy: the step lives on
    /// the caller's stack for exactly as long as the dispatch.
    class HWSHQTB_GUI_EXPORT interact_event_t final: public ::hwshqtb::metaobject::event_t {
    public:
        /// @brief Construct for @p step.
        /// @param step The converted step, in the pixel space.
        /// @note noexcept.
        explicit interact_event_t(const interaction_t& step) noexcept;

        /// @brief The input step, in the pixel space.
        /// @return The step given at construction.
        /// @note noexcept.
        const interaction_t& step() const noexcept;

    private:
        /// @brief The window's converted step; borrowed for the dispatch only.
        const interaction_t& _step;
    };

    /// @brief The window is closing (the user asked for it, or close() was called).
    /// @note No payload: everything a receiver needs is on the window, and the default hook is what
    ///       decides whether the close happens.
    class HWSHQTB_GUI_EXPORT close_event_t final: public ::hwshqtb::metaobject::event_t {
    public:
        /// @brief Construct a close event.
        /// @note noexcept.
        close_event_t() noexcept;
    };

    /// @brief The window lost the keyboard focus (QWindow::focusOutEvent equivalent).
    /// @note No payload: the window is the subject, and a window has no "reason" the way a widget
    ///       focus event does.
    class HWSHQTB_GUI_EXPORT focus_out_event_t final: public ::hwshqtb::metaobject::event_t {
    public:
        /// @brief Construct a focus-out event.
        /// @note noexcept.
        focus_out_event_t() noexcept;
    };

    /// @brief The pointer left the client area (QWidget::leaveEvent equivalent).
    ///
    /// @par Why the name
    /// Qt names this pair `enterEvent` / `leaveEvent` and carries them on the WIDGET, because a window
    /// system reports the pointer position and the framework decides what it left. This layer keeps the
    /// same name for the same reason: "leave" is the concept, and "the pointer" is who left.
    /// @note Pair of `event_type_enum::ENTER`; a window reports both as core event codes, because the
    ///       pointer crossing the client area is a window-system fact, not a ui state change.
    /// @note No payload: Qt's leaveEvent carries a bare QEvent too; the position is already in
    ///       input_state().
    class HWSHQTB_GUI_EXPORT leave_event_t final: public ::hwshqtb::metaobject::event_t {
    public:
        /// @brief Construct a leave event.
        /// @note noexcept.
        leave_event_t() noexcept;
    };

    /// @brief A window was resized (QResizeEvent equivalent).
    ///
    /// It carries the size BEFORE the change and the size AFTER it, so an override can tell a grow
    /// from a shrink without remembering the previous size itself. When several pending resize
    /// events are merged, @c old_size() stays the size before the FIRST pending change and
    /// @c size() becomes the LATEST size: the receiver sees one net change, not every intermediate
    /// step.
    ///
    /// Both sizes are in PIXELS, the renderer's output size (window_t::size()): the space the item
    /// tree, damage and painting live in. The platform's window coordinates are a different space and
    /// an event never carries them.
    /// @note Only events still pending in the dispatcher queue are merged; an event that has already
    ///       been delivered is never rewritten.
    class HWSHQTB_GUI_EXPORT resize_event_t final: public ::hwshqtb::metaobject::event_t {
    public:
        /// @brief Construct a resize event.
        /// @param old_size The size before the change, in pixels.
        /// @param size The size after the change, in pixels.
        /// @note noexcept.
        resize_event_t(extent_t old_size, extent_t size) noexcept;

        /// @brief The size before the first pending change.
        /// @return The size at construction, or the value kept by the earliest merged event; in pixels.
        /// @note noexcept.
        extent_t old_size() const noexcept;

        /// @brief The latest size.
        /// @return The size at construction, or the newest size merged into this event; in pixels.
        /// @note noexcept.
        extent_t size() const noexcept;

        /// @brief Fold a later resize event into this one.
        /// @param newer A later event of the same type and receiver.
        /// @return true when @p newer is a resize_event_t and was absorbed.
        /// @note @c old_size() keeps the EARLIEST value and @c size() takes the LATEST one.
        bool merge_with(const ::hwshqtb::metaobject::event_t& newer) override;

    private:
        /// @brief The size before the first pending change.
        extent_t _old_size;
        /// @brief The latest size.
        extent_t _size;
    };

    /// @brief A window was moved (QMoveEvent equivalent).
    ///
    /// It carries the position BEFORE the change and the position AFTER it, in global screen
    /// coordinates. Merging keeps the earliest old position and the latest position, the same rule
    /// as resize_event_t.
    class HWSHQTB_GUI_EXPORT move_event_t final: public ::hwshqtb::metaobject::event_t {
    public:
        /// @brief Construct a move event.
        /// @param old_position The position before the change.
        /// @param position The position after the change.
        /// @note noexcept.
        move_event_t(point_t old_position, point_t position) noexcept;

        /// @brief The position before the first pending change.
        /// @return The position at construction, or the value kept by the earliest merged event.
        /// @note noexcept.
        point_t old_position() const noexcept;

        /// @brief The latest position.
        /// @return The position at construction, or the newest position merged into this event.
        /// @note noexcept.
        point_t position() const noexcept;

        /// @brief Fold a later move event into this one.
        /// @param newer A later event of the same type and receiver.
        /// @return true when @p newer is a move_event_t and was absorbed.
        /// @note @c old_position() keeps the EARLIEST value and @c position() takes the LATEST one.
        bool merge_with(const ::hwshqtb::metaobject::event_t& newer) override;

    private:
        /// @brief The position before the first pending change.
        point_t _old_position;
        /// @brief The latest position.
        point_t _position;
    };

    /// @brief The window system asks for a redraw (QExposeEvent equivalent).
    ///
    /// The event has no payload: it only says that the surface may have lost its contents. Several
    /// pending expose events merge into the FIRST one (the inherited event_t::merge_with behaviour),
    /// because a single redraw satisfies all of them.
    class HWSHQTB_GUI_EXPORT expose_event_t final: public ::hwshqtb::metaobject::event_t {
    public:
        /// @brief Construct an expose event.
        /// @note noexcept.
        expose_event_t() noexcept;
    };
}

#endif
