#ifndef HWSHQTB__UI__PROGRESS_BAR_T_H
#define HWSHQTB__UI__PROGRESS_BAR_T_H

/// @file progress_bar_t.h
/// @brief progress_bar_t: a groove and a proportionally scaled chunk (the QProgressBar subset of the
///        ui layer).

#include <cstdint>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/orientation.h"
#include "hwshqtb/ui/widget_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief A progress bar: the value is clamped to the range and drawn as a scaled chunk.
    ///
    /// @par Not interactive
    /// A progress bar is passive: it is not focusable (its focus policy stays no_focus), it consumes
    /// no interaction (it has no interact() override) and it declares no User property.
    ///
    /// @par The value model
    /// minimum, maximum and value are persistent state, and every one of them has a setter:
    /// set_range() installs the bounds (swapping them when they arrive inverted) and set_value()
    /// clamps into them, so value() always lies in [minimum(), maximum()]. A value outside the range
    /// is CLAMPED, never stored: the bar keeps one invariant instead of trusting its caller.
    ///
    /// @par Geometry
    /// The groove is centred on the short axis and spans the long one, at most PROGRESS_GROOVE_HEIGHT
    /// pixels thick. The chunk fills the groove inside its border; its long extent is
    /// inner_extent * (value - minimum) / (maximum - minimum), and an empty value range draws no chunk
    /// at all.
    ///
    /// @par Painting
    /// paint_event() draws the groove and the chunk through default_style(), so the bar names no
    /// colour and no pixel value.
    class HWSHQTB_UI_EXPORT progress_bar_t: public widget_t {
        OBJECT

    public:
        /// @brief Construct a bar with no visual parent: range [0, 100], value 0, horizontal.
        progress_bar_t();

        /// @brief Construct a bar inside a retained widget tree.
        /// @param parent The visual parent; nullptr makes this a root widget.
        explicit progress_bar_t(widget_t* parent);

        /// @brief The lower bound (0 by default).
        int minimum() const;

        /// @brief The upper bound (100 by default).
        int maximum() const;

        /// @brief Install the value range and clamp the current value into it.
        /// @param minimum The lower bound.
        /// @param maximum The upper bound; when it is smaller than @p minimum the two are swapped, so
        ///        the bar always has a well-formed range.
        /// @note Emits value_changed() when the new range moves the current value.
        void set_range(int minimum, int maximum);

        /// @brief The current value (always inside [minimum(), maximum()]).
        int value() const;

        /// @brief Set the value, clamped into [minimum(), maximum()].
        /// @param value The new value; a value outside the range is clamped, not rejected.
        /// @note Emits value_changed() only when the stored value actually changes.
        void set_value(int value);

        /// @brief The growth direction (horizontal by default).
        orientation_enum direction() const;

        /// @brief Set the growth direction.
        /// @param direction The new direction (see orientation_enum; a vertical bar grows upwards).
        void set_direction(orientation_enum direction);

        /// @brief The natural size, derived from the style alone (a bar has no content).
        /// @return Horizontal: {PROGRESS_BAR_WIDTH, PROGRESS_GROOVE_HEIGHT + 2 * BORDER_WIDTH};
        ///         vertical: the two swapped.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief Emitted when the stored value changes.
        /// @param value The new value.
        SIGNAL(void value_changed(int value) const)

    protected:
        /// @brief Draw the groove and the scaled chunk through default_style().
        /// @param event The paint event; event.painter() is already translated to this widget and
        ///        clipped to rect().
        void paint_event(paint_event_t* event) override;

    private:
        /// @brief The lower bound of the value range.
        int _minimum = 0;
        /// @brief The upper bound of the value range (never below _minimum).
        int _maximum = 100;
        /// @brief The current value, always inside the range.
        int _value = 0;
        /// @brief The growth direction.
        orientation_enum _direction = orientation_enum::HORIZONTAL;
    };
}

#endif
