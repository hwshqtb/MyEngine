#ifndef HWSHQTB__UI__SCROLL_BAR_T_H
#define HWSHQTB__UI__SCROLL_BAR_T_H

/// @file scroll_bar_t.h
/// @brief scroll_bar_t: a groove plus a proportional handle (the QScrollBar subset of the ui layer).

#include <cstdint>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/orientation.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief A scroll bar: a groove with a proportional, draggable handle.
    ///
    /// @par The value and handle model
    /// The stored value always lies in [minimum(), maximum()]: set_value() and set_range() clamp.
    /// The handle length is proportional to page_step() against the range
    /// (`track * page_step / (range + page_step)`, bounded below by the style's minimum handle
    /// length); its position maps the value over the track minus the handle, so value == maximum puts
    /// the handle at the far end.
    ///
    /// @par Interaction (Qt's QScrollBar subset)
    ///  - press on the HANDLE: start dragging it; while held, the handle follows the pointer (the grab
    ///    offset inside the handle is preserved) and the value follows the handle position;
    ///  - press on the GROOVE outside the handle: page one step in that direction (before the handle
    ///    decreases, after it increases), no drag — Qt's left-click behaviour;
    ///  - keyboard (with the focus): Left/Up decrement single_step(), Right/Down increment it,
    ///    PageUp/PageDown decrement/increment page_step(), Home/End jump to the bounds. The step is
    ///    consumed even when the value cannot move;
    ///  - disabled: consumes nothing.
    ///
    /// @par State changes versus the frame's event
    /// The setters announce a real state change with value_changed(int) — the SAME signal the control
    /// emits for the user's own actions — and they never raise the User property. value_changed (the
    /// User event) is true only on the frame the CONTROL changed the value (a drag, a groove page or a
    /// keyboard step). The two channels answer different questions: the signal is a state
    /// notification from any origin, the User event is the interaction result.
    ///
    /// @par Painting and size
    /// The groove is the whole rectangle and the handle lies inside it; both are drawn by
    /// default_style(). The natural size is SCROLL_BAR_LENGTH along the travel axis and
    /// SCROLL_BAR_THICKNESS across it.
    class HWSHQTB_UI_EXPORT scroll_bar_t: public widget_t {
        OBJECT

    public:
        /// @brief Construct a vertical scroll bar with no visual parent: range [0, 100], value 0.
        /// @note page_step() is 10, single_step() is 1 and the focus policy is
        ///       focus_policy_enum::STRONG_FOCUS (an interactive control).
        scroll_bar_t();

        /// @brief Construct a scroll bar inside a retained widget tree (vertical by default).
        /// @param parent The visual parent; nullptr makes this a root widget.
        explicit scroll_bar_t(widget_t* parent);

        /// @brief The lower bound (0 by default).
        int minimum() const;

        /// @brief The upper bound (100 by default).
        int maximum() const;

        /// @brief Install the value range and clamp the current value into it.
        /// @param minimum The lower bound.
        /// @param maximum The upper bound; when it is smaller than @p minimum the two are swapped.
        /// @note Emits value_changed() when the clamp moves the stored value, and never raises the
        ///       User event (the caller owns this write).
        void set_range(int minimum, int maximum);

        /// @brief The current value (always inside [minimum(), maximum()]).
        /// @note The `value` property; the writer is set_value() and the notifier is value_changed().
        /// @note The immediate host restores this property through set_value(), before a caller can
        ///       install a non-default range, so a stored value outside the DEFAULT range is clamped
        ///       to it. A caller with another range must set the range and re-feed the value every
        ///       pass; set_range() afterwards only clamps the value it already sees.
        [[= ::hwshqtb::metaobject::make_property("value", "set_value", "value_changed")]]
        int value() const;

        /// @brief Set the value, clamped into [minimum(), maximum()].
        /// @param value The new value.
        /// @note Emits value_changed() when the value actually changes (a state notification) and
        ///       never raises the User event: the caller's write is not a user action.
        void set_value(int value);

        /// @brief The Page Up / Page Down step (10 by default).
        int page_step() const;

        /// @brief Set the Page Up / Page Down step (it also sizes the handle).
        /// @param step The step; a negative value is treated as 0.
        void set_page_step(int step);

        /// @brief The arrow-key step (1 by default).
        int single_step() const;

        /// @brief Set the arrow-key step.
        /// @param step The step; a negative value is treated as 0.
        void set_single_step(int step);

        /// @brief The axis the handle travels along.
        orientation_enum orientation() const;

        /// @brief Set the axis the handle travels along.
        /// @param orientation The new orientation (vertical by default).
        void set_orientation(orientation_enum orientation);

        /// @brief Whether this frame changed the value by the CONTROL's own action.
        /// @return true only on a frame where a drag, a groove page or a keyboard step changed the
        ///         value; false otherwise (including frames whose value was written through the
        ///         setters).
        /// @note The class's User property (property_trait_flag::USER): the immediate host's control()
        ///       returns exactly this value.
        [[= ::hwshqtb::metaobject::make_property("value_changed", "", "", "", ::hwshqtb::metaobject::property_trait_flag::USER)]]
        bool value_changed() const;

        /// @brief The natural size from the style alone.
        /// @return Horizontal: {SCROLL_BAR_LENGTH, SCROLL_BAR_THICKNESS}; vertical: the two swapped.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief Drive the drag/page/keyboard state machine one frame.
        /// @param in The input snapshot, in this widget's local coordinates (the host maps it).
        /// @return true while the bar owns the input: on the press, on every dragged frame, on the
        ///         release, and on a recognised key while it holds the focus. Disabled: false.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Emitted when the control itself changed the value.
        /// @param value The new value.
        SIGNAL(void value_changed(int value) const)

    protected:
        /// @brief Draw the groove and the handle through default_style().
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

    private:
        /// @brief The handle rectangle for the current value.
        ::hwshqtb::gui::rect_t handle_rect() const;

        /// @brief The long-axis length of the handle (proportional to the page step, bounded below).
        int handle_length() const;

        /// @brief The long-axis extent of the whole bar (its track).
        int track_length() const;

        /// @brief How far the handle may travel: the track minus the handle.
        int travel() const;

        /// @brief The handle's long-axis offset for the current value.
        int handle_start() const;

        /// @brief The long-axis coordinate of @p local.
        int axis_position(::hwshqtb::gui::point_t local) const;

        /// @brief The value a handle starting at @p start represents (rounded, clamped).
        int value_from_handle_start(int start) const;

        /// @brief Apply a value the CONTROL produced: clamp, then announce the real change only.
        /// @param value The wanted value.
        void set_value_from_user(int value);

        /// @brief The lower bound of the value range.
        int _minimum = 0;
        /// @brief The upper bound of the value range (never below _minimum).
        int _maximum = 100;
        /// @brief The current value, always inside the range.
        int _value = 0;
        /// @brief The Page Up / Page Down step (and the handle-size basis).
        int _page_step = 10;
        /// @brief The arrow-key step.
        int _single_step = 1;
        /// @brief The axis the handle travels along.
        orientation_enum _orientation = orientation_enum::VERTICAL;
        /// @brief Whether the pointer was inside this control in the last interact().
        bool _hovered = false;
        /// @brief Whether a press on the handle started a drag that has not ended yet.
        bool _dragging = false;
        /// @brief Where inside the handle the drag started (long-axis pixels, may be negative).
        int _drag_offset = 0;
        /// @brief Whether this frame changed the value (the User property).
        bool _value_changed = false;
    };
}

#endif
