#ifndef HWSHQTB__UI__STYLE_T_H
#define HWSHQTB__UI__STYLE_T_H

/// @file style_t.h
/// @brief style_t: the QStyle subset every control draws through — pixel metrics and element
///        painting — plus default_style(), the process-wide concrete style.

#include <cstdint>
#include <string_view>

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/ui/export.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief One length a control asks the style for instead of writing it down itself.
    ///
    /// @par Why metrics are an enum, not constants
    /// A control never hard-codes a pixel value: it asks pixel_metric(). A different style therefore
    /// changes spacing and sizes without touching a single control, exactly like QStyle::pixelMetric.
    enum struct pixel_metric_enum : std::uint8_t {
        /// @brief The horizontal gap between text and the control's edge (button, label, check box).
        TEXT_PADDING,
        /// @brief The vertical gap above and below one line of text (label, check box).
        TEXT_VERTICAL_PADDING,
        /// @brief The thickness of a one-pixel frame drawn by draw_rect (reserved inset of a frame).
        BORDER_WIDTH,
        /// @brief The fixed height of a push button (its label is one line, so the height is fixed).
        BUTTON_HEIGHT,
        /// @brief The edge length of a check box indicator (it is square).
        INDICATOR_SIZE,
        /// @brief The horizontal gap between a check box indicator and its text.
        INDICATOR_SPACING,
        /// @brief The thickness of the outline the focus_rect element draws.
        FOCUS_RECT_WIDTH,
        /// @brief The height of a progress bar's groove (the bar is horizontal by default).
        PROGRESS_GROOVE_HEIGHT,
        /// @brief The natural width of a progress bar: it has no text to derive one from.
        PROGRESS_BAR_WIDTH,
        /// @brief The thickness of a slider groove.
        SLIDER_GROOVE_THICKNESS,
        /// @brief The edge length of a slider handle (it is square).
        SLIDER_HANDLE_SIZE,
        /// @brief The natural length of a slider: it has no text to derive one from.
        SLIDER_LENGTH,
        /// @brief The cross-axis thickness of a scroll bar.
        SCROLL_BAR_THICKNESS,
        /// @brief The minimum long-axis length of a scroll bar handle.
        SCROLL_BAR_MIN_HANDLE,
        /// @brief The natural length of a scroll bar: it has no text to derive one from.
        SCROLL_BAR_LENGTH,
        /// @brief The number of pixels one wheel tick scrolls a scroll area.
        SCROLL_WHEEL_STEP,
        /// @brief The height of one standard interactive row (check box); rows of different controls
        ///        line up because they share this metric.
        LINE_HEIGHT,
        /// @brief The height of a group box's title strip (and of the content inset it reserves).
        TITLE_HEIGHT,
        /// @brief The height of a tab widget's tab bar.
        TAB_HEIGHT,
        /// @brief The minimum width of a line edit (its natural width follows the text, but never goes
        ///        below this).
        LINE_EDIT_WIDTH,
        /// @brief The minimum usable width of a list view: narrower than this and neither a row nor
        ///        the scroll bar beside it fits.
        LIST_VIEW_MIN_WIDTH,
        /// @brief The natural width of a list view (a list has no content-derived width).
        LIST_VIEW_WIDTH,
        /// @brief The natural height of a list view (a list has no content-derived height).
        LIST_VIEW_HEIGHT,
        /// @brief The width of a table column no program set through set_column_width().
        TABLE_COLUMN_WIDTH,
        /// @brief The height of a table's header strip.
        TABLE_HEADER_HEIGHT,
        /// @brief The widest width a table asks to be: size_hint() caps the column widths here.
        TABLE_MAX_WIDTH,
        /// @brief The thickness of the grid line between two table columns.
        TABLE_GRID_WIDTH,
        /// @brief The horizontal distance one level of a tree indents its rows.
        TREE_INDENTATION,
        /// @brief The edge length of a tree row's expander (it is square).
        TREE_EXPANDER_SIZE,
    };

    /// @brief One drawable part of a control. A control calls draw_control() once per part it shows
    ///        and passes the whole state, so the style decides the colours and the feedback.
    enum struct control_element_enum : std::uint8_t {
        /// @brief The background plus the one-pixel border of a push button.
        BUTTON_FRAME,
        /// @brief A check box indicator, including the tick when the state is checked.
        CHECK_INDICATOR,
        /// @brief A radio button indicator, including the dot when the state is checked.
        RADIO_INDICATOR,
        /// @brief The empty track of a progress bar.
        PROGRESS_GROOVE,
        /// @brief The filled part of a progress bar; the caller passes the already scaled rectangle.
        PROGRESS_CHUNK,
        /// @brief The track of a slider.
        SLIDER_GROOVE,
        /// @brief The draggable handle of a slider.
        SLIDER_HANDLE,
        /// @brief The track of a scroll bar.
        SCROLL_BAR_GROOVE,
        /// @brief The draggable handle of a scroll bar.
        SCROLL_BAR_HANDLE,
        /// @brief The background (and one-pixel border) of a scroll view's viewport.
        SCROLL_VIEW_VIEWPORT,
        /// @brief The background of one unselected list row (state.hovered brightens it).
        LIST_ROW,
        /// @brief The background of one selected list row (state.hovered brightens it).
        LIST_ROW_SELECTED,
        /// @brief The outline around the list's current row (state.focused says the list has the focus).
        LIST_ROW_CURRENT,
        /// @brief The text of one list row, left-aligned inside the passed rectangle.
        LIST_TEXT,
        /// @brief One table header cell: the background, the top strip's bottom border and the column
        ///        title (state.text, left-aligned with TEXT_PADDING).
        TABLE_HEADER,
        /// @brief The content of one table cell: state.text, left-aligned with TEXT_PADDING, plus a
        ///        one-pixel outline around the cell when state.focused (the current cell).
        TABLE_CELL,
        /// @brief One grid line of a table: a filled rectangle the caller sizes with TABLE_GRID_WIDTH.
        TABLE_GRID,
        /// @brief A tree row's expander: a square box whose marker shows whether the node is expanded.
        ///        state.hovered brightens the box; state.checked draws the expanded form (one stroke),
        ///        the unchecked box is the collapsed one (both strokes).
        TREE_EXPANDER,
        /// @brief A one-pixel focus outline; the caller passes the rectangle to outline.
        FOCUS_RECT,
        /// @brief The background and one-pixel border of a group box (the whole rectangle).
        GROUP_BOX_FRAME,
        /// @brief The title text of a group box, centred inside the passed title rectangle.
        GROUP_BOX_TITLE_TEXT,
        /// @brief The background strip of a tab widget's tab bar.
        TAB_BAR,
        /// @brief One tab: its background, its border and its title (state.checked = selected).
        TAB,
        /// @brief The background and border of a line edit (state.focused brightens the border).
        LINE_EDIT_FRAME,
        /// @brief The selected-text highlight of a line edit (a filled rectangle).
        SELECTION,
        /// @brief The one-pixel caret of a line edit (a filled rectangle).
        LINE_EDIT_CURSOR,
        /// @brief The text of a control, centred inside the passed rectangle.
        TEXT,
    };

    /// @brief The state a control passes to draw_control(): which feedback applies, plus the text the
    ///        text element has to draw.
    ///
    /// @par Text
    /// state.text is the string the TEXT element draws; every other element ignores it. It lives in
    /// the state because the two-entry style API has no separate text argument: keeping one
    /// draw_control() signature is what makes a style a plain function of (element, rectangle, state).
    struct control_state_t {
        /// @brief Whether the control is enabled; a disabled control is drawn in the disabled palette.
        bool enabled = true;
        /// @brief Whether the pointer is inside the control.
        bool hovered = false;
        /// @brief Whether the control is held down.
        bool pressed = false;
        /// @brief Whether the control holds the keyboard focus.
        bool focused = false;
        /// @brief Whether the control is checked (check box).
        bool checked = false;
        /// @brief The text the TEXT element draws (empty for the other elements).
        /// @note The view is valid only for the duration of the draw_control() call: a style must draw
        ///       the text and must not keep the string_view.
        std::string_view text{};
    };

    /// @brief The drawing policy of the ui layer: pixel metrics and element painting (the QStyle
    ///        subset this layer needs).
    ///
    /// @par Two entry points
    /// A control asks pixel_metric() for every length and draw_control() for every painted part, and
    /// otherwise writes no colour and no pixel value. A style is therefore a value-like object: it
    /// owns no widget, holds no per-control state, and every member is const.
    ///
    /// @par Drawing contract
    /// draw_control() draws into the painter it is given, in the painter's CURRENT user coordinates
    /// (the widget has already translated and clipped). It may change the painter's current colour;
    /// it never saves or restores the painter state, and it never draws outside @p rect.
    ///
    /// @par Lifetime
    /// A style must outlive every control drawn with it. The controls of this layer draw with
    /// default_style(), the process-wide instance, and never own a style.
    class HWSHQTB_UI_EXPORT style_t {
    public:
        /// @brief Destroy the style (a control must not outlive its style).
        virtual ~style_t() = default;

        /// @brief The length of @p metric in pixels.
        /// @param metric The metric to look up.
        /// @return The length; always positive for a metric a control divides by.
        /// @note noexcept: a style answers from constants, so a layout path cannot throw.
        /// @note The default forwards to default_style(): a style overrides only the metrics it means
        ///       to change and inherits every other one instead of having to repeat them.
        /// @note This is an EXTENSION POINT: it is one of the two questions a control asks a style.
        virtual int pixel_metric(pixel_metric_enum metric) const noexcept;

        /// @brief Draw one element of a control.
        /// @param element The part to draw.
        /// @param painter The painter of the current frame (already translated and clipped).
        /// @param rect The rectangle of the element in the painter's current coordinates; an empty
        ///        rectangle draws nothing.
        /// @param state The control's state (enabled/hovered/pressed/focused/checked plus the text).
        /// @note The default forwards to default_style(), so a style that customizes one element
        ///       draws the rest exactly as the process-wide style does.
        /// @note This is an EXTENSION POINT: it is one of the two questions a control asks a style.
        virtual void draw_control(control_element_enum element, ::hwshqtb::gui::painter_t& painter,
                                  ::hwshqtb::gui::rect_t rect, const control_state_t& state) const;

        /// @brief The character a password field shows instead of each real one.
        /// @return The placeholder character; '*' by default.
        /// @note Not one of the two drawing entries: this is a CHARACTER the style owns, like a metric,
        ///       and it cannot be expressed as an int. The default is what a style that does not care
        ///       keeps.
        /// @note This is an EXTENSION POINT, like the two entries above.
        virtual char password_character() const noexcept {
            return '*';
        }
    };

    /// @brief The concrete style of the ui layer: the slate palette and the metrics the controls were
    ///        designed with.
    ///
    /// @par Appearance
    /// Buttons use a slate fill with a light border, check boxes a blue accent when checked, progress
    /// bars a dark groove with a blue chunk. Every colour lives here; a control never names one.
    ///
    /// @par Stateless
    /// The class holds nothing, so the process-wide instance in default_style() is safe to share and
    /// two instances draw identically.
    class HWSHQTB_UI_EXPORT default_style_t final: public style_t {
    public:
        /// @brief Construct the style (it has no configuration; all values are fixed).
        default_style_t() = default;

        /// @brief The length of @p metric in pixels.
        /// @param metric The metric to look up.
        /// @return The value of the slate layout; every enumerator of pixel_metric_enum is covered.
        /// @note noexcept.
        int pixel_metric(pixel_metric_enum metric) const noexcept override;

        /// @brief Draw one element with the slate palette.
        /// @param element The part to draw.
        /// @param painter The painter of the current frame.
        /// @param rect The element rectangle, in the painter's current coordinates.
        /// @param state The control's state.
        void draw_control(control_element_enum element, ::hwshqtb::gui::painter_t& painter,
                          ::hwshqtb::gui::rect_t rect, const control_state_t& state) const override;

    private:
        /// @brief The metrics the font-derived text metrics are computed from.
        /// @note A cache, not state: pixel_metric() runs per row per frame, and resolving the default
        ///       family every call would hit the font database (and possibly GDI) thousands of times per
        ///       paint.
        mutable ::hwshqtb::gui::font_metrics_t _font_metrics{};
        /// @brief The font database generation _font_metrics reflects.
        mutable unsigned _font_generation = 0;
        /// @brief Whether _font_generation has been recorded at least once.
        mutable bool _font_cached = false;

        /// @brief The cached metrics, rebuilt when the font database's generation moved.
        /// @return The metrics of the CURRENT default family.
        /// @note Thread safety: the ui layer resolves fonts and paints on one thread, so the mutable
        ///       cache needs no lock. A second thread calling pixel_metric() would be a bug anyway.
        const ::hwshqtb::gui::font_metrics_t& font_metrics() const noexcept;
    };

    /// @brief The one style instance of the process.
    /// @return A reference to a function-local static default_style_t; it is constructed on first use
    ///         and lives until the process ends, so it can be handed to any control.
    /// @note noexcept. The reference stays valid for the whole process.
    HWSHQTB_UI_EXPORT const style_t& default_style() noexcept;
}

#endif
