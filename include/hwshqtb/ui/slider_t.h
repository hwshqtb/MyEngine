#ifndef HWSHQTB__UI__SLIDER_T_H
#define HWSHQTB__UI__SLIDER_T_H

/// @file slider_t.h
/// @brief slider_t: a groove plus a draggable handle (the QSlider subset of the ui layer).

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
    /// @brief A slider: a groove the handle slides in, driven by the mouse or the keyboard.
    ///
    /// @par The value model
    /// minimum, maximum, value, single_step and page_step are persistent state, and every one of them
    /// has a setter. The stored value ALWAYS lies in [minimum(), maximum()]: set_value() and
    /// set_range() clamp, they never reject.
    ///
    /// @par State changes versus the frame's event
    /// The setters announce a real state change with value_changed(int) — the SAME signal the control
    /// emits for the user's own actions — and they never raise the User property. value_changed (the
    /// User event) is true only on the frame the CONTROL changed the value (a click, a drag or a
    /// keyboard step). The two channels answer different questions: the signal is a state
    /// notification from any origin, the User event is the interaction result.
    ///
    /// @par The drag state machine
    ///  - press inside: jump to the pointer's value (Qt's snap-to-position behaviour), remember the
    ///    drag, emit slider_pressed() and consume;
    ///  - while dragged: every frame re-reads the pointer (outside the widget too — the position is
    ///    clamped to the range) and updates the value continuously. There is NO step alignment: the
    ///    value is continuous, only single_step()/page_step() bound the keyboard;
    ///  - release: update once more, emit slider_released() and consume;
    ///  - the mouse position maps linearly onto the handle's travel; a value change emits
    ///    value_changed() and raises the User event;
    ///  - keyboard (with the focus): Left/Down decrement single_step(), Right/Up increment it,
    ///    PageDown/PageUp decrement/increment page_step(), Home/End jump to the bounds. The arrows
    ///    move the value, not the focus; a step that does not change the value emits nothing.
    ///
    /// @par Painting and size
    /// paint_event() draws the groove and the handle through default_style(); the handle is centred
    /// on the value's position along the groove (a vertical slider's minimum is at the bottom).
    class HWSHQTB_UI_EXPORT slider_t: public widget_t {
        OBJECT

    public:
        /// @brief Construct a slider with no visual parent: range [0, 100], value 0, horizontal.
        /// @note single_step() is 1 and page_step() is 10; the focus policy is
        ///       focus_policy_enum::STRONG_FOCUS (an interactive control).
        slider_t();

        /// @brief Construct a slider inside a retained widget tree.
        /// @param parent The visual parent; nullptr makes this a root widget.
        explicit slider_t(widget_t* parent);

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

        /// @brief The keyboard/arrow step (1 by default).
        int single_step() const;

        /// @brief Set the keyboard/arrow step.
        /// @param step The step; a negative value is treated as 0.
        void set_single_step(int step);

        /// @brief The Page Up / Page Down step (10 by default).
        int page_step() const;

        /// @brief Set the Page Up / Page Down step.
        /// @param step The step; a negative value is treated as 0.
        void set_page_step(int step);

        /// @brief The axis the handle travels along.
        orientation_enum orientation() const;

        /// @brief Set the axis the handle travels along.
        /// @param orientation The new orientation (horizontal by default).
        void set_orientation(orientation_enum orientation);

        /// @brief Whether this frame changed the value by the CONTROL's own action.
        /// @return true only on a frame where a click, a drag or a keyboard step changed the value;
        ///         false otherwise (including frames whose value was written through set_value()).
        /// @note The class's User property (property_trait_flag::USER): the immediate host's control()
        ///       returns exactly this value.
        [[= ::hwshqtb::metaobject::make_property("value_changed", "", "", "", ::hwshqtb::metaobject::property_trait_flag::USER)]]
        bool value_changed() const;

        /// @brief The natural size from the style alone: SLIDER_LENGTH along the travel axis, and the
        ///        larger of the handle and the groove across it.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief Drive the drag/keyboard state machine one frame.
        /// @param in The input snapshot, in this widget's local coordinates (the host maps it).
        /// @return true while the slider owns the input: on the press, on every dragged frame, on the
        ///         release, and on a recognised key while it holds the focus. Disabled: false.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Emitted when the control itself changed the value.
        /// @param value The new value.
        SIGNAL(void value_changed(int value) const)

        /// @brief Emitted when the mouse press started a drag.
        SIGNAL(void slider_pressed() const)

        /// @brief Emitted when the mouse release ended a drag (the value has already been updated).
        SIGNAL(void slider_released() const)

    protected:
        /// @brief Draw the groove and the handle through default_style().
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

    private:
        /// @brief The groove rectangle: the long axis spans the widget, the short axis is centred.
        ::hwshqtb::gui::rect_t groove_rect() const;

        /// @brief The handle rectangle for the current value (centred on its travel position).
        ::hwshqtb::gui::rect_t handle_rect() const;

        /// @brief The value a pointer at @p local maps to (rounded to the nearest step, clamped).
        /// @param local The pointer position in this widget's coordinates.
        int value_from_position(::hwshqtb::gui::point_t local) const;

        /// @brief Apply a value the CONTROL produced: clamp, then announce the real change only.
        /// @param value The wanted value.
        void set_value_from_user(int value);

        /// @brief The lower bound of the value range.
        int _minimum = 0;
        /// @brief The upper bound of the value range (never below _minimum).
        int _maximum = 100;
        /// @brief The current value, always inside the range.
        int _value = 0;
        /// @brief The arrow-key step.
        int _single_step = 1;
        /// @brief The Page Up / Page Down step.
        int _page_step = 10;
        /// @brief The axis the handle travels along.
        orientation_enum _orientation = orientation_enum::HORIZONTAL;
        /// @brief Whether the pointer was inside this control in the last interact().
        bool _hovered = false;
        /// @brief Whether a press started a drag that has not ended yet.
        bool _dragging = false;
        /// @brief Whether this frame changed the value (the User property).
        bool _value_changed = false;
    };
}

#endif
