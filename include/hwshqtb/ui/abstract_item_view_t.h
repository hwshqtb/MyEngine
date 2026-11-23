#ifndef HWSHQTB__UI__ABSTRACT_ITEM_VIEW_T_H
#define HWSHQTB__UI__ABSTRACT_ITEM_VIEW_T_H

/// @file abstract_item_view_t.h
/// @brief abstract_item_view_t: the row viewport every virtualised item view shares.

#include <cstdint>
#include <vector>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/ui/export.h"
#include "hwshqtb/ui/item_model_t.h"
#include "hwshqtb/ui/scroll_view_t.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {

    /// @brief How many rows an item view may select at once.
    enum struct selection_mode_enum : std::uint8_t {
        /// @brief The current row is the only selected row (the default).
        SINGLE,
        /// @brief Any number of rows may be selected; Ctrl toggles and Shift extends from the anchor.
        MULTI,
    };

    /// @brief The row grid shared by every virtualised item view: per-row damage, the selection, the
    ///        current row, the hovered row, the keyboard and the model wiring.
    ///
    /// @par One implementation, many views
    /// Everything a scrolling row view needs except "what does one row look like" lives here. A
    /// subclass answers one question: paint_row() — the pixels of one row — and, when its content can
    /// be wider than the view, content_width() — the extent the horizontal range derives from. The
    /// base keeps the whole state machine, so the views cannot disagree about what a click, a key or
    /// a model notification means.
    ///
    /// @par Rows are data, not item children
    /// The view does not create an item object per row: rows live only in the model and the view
    /// paints the visible band by asking the model for it. The row band itself is drawn by one
    /// private content item — a child that refers back to the view and paints the visible rows in
    /// content coordinates — so the scroll path moves the SOURCE RECTANGLE and never a row item.
    /// Hover, press, capture and focus are the view ITEM's state; the selected set, the current row
    /// and the scroll offset are view state.
    ///
    /// @par Virtualisation
    /// A paint pass asks the model only for the rows that intersect the destination, and scrolling
    /// uses the pixel-shift fast path of scroll_view_t rather than repainting the destination. The
    /// cost of a frame is therefore independent of the model's row count.
    ///
    /// @par Fixed row height
    /// All rows have the same height (set_row_height(); the default is the style's LINE_HEIGHT plus
    /// the vertical text padding). The visible range is a division by that height, floored to whole
    /// rows, and the vertical scroll range is `max(0, visible_row_count * row_height - destination
    /// height)`.
    ///
    /// @par The rows the view shows
    /// Row numbers are the rows of the view, not the model's top-level rows: visible_row_count()
    /// answers the count and a plain row view answers the model's row_count(). A subclass that shows a
    /// PROJECTION of the model overrides it — a tree flattens the expanded nodes — and every row
    /// operation (painting, hit testing, damage, keyboard, selection, scroll range) follows, so one
    /// row grid serves a list, a table and a tree.
    ///
    /// @par What a subclass inherits
    /// The model reference and its five connections, set_model(), the row height, the current row,
    /// the selection in both modes, scroll_to_row(), minimum_size_hint(), paint() and interact(),
    /// together with the three signals below. A subclass whose rows are derived rather than read also
    /// gets the row-level handlers: visible_row_count(), reset_contents(), data_change_event(),
    /// rows_change_event(), remap_rows() and the row_at()/row_rect() geometry. The viewport —
    /// destination, bars, offsets and the scroll request — comes from scroll_view_t; a subclass never
    /// touches a scroll bar.
    class HWSHQTB_UI_EXPORT abstract_item_view_t: public scroll_view_t {
        OBJECT

    public:
        /// @brief Construct an empty view with no model, no current row and no selection.
        /// @param parent The visual parent; nullptr makes the view a root widget.
        /// @note The focus policy is focus_policy_enum::STRONG_FOCUS (the keyboard is part of the
        ///       view's input contract) and the row height starts at the style default.
        explicit abstract_item_view_t(widget_t* parent = nullptr);

        /// @brief Disconnect from the model.
        ~abstract_item_view_t() override;

        abstract_item_view_t(const abstract_item_view_t&) = delete;
        abstract_item_view_t& operator=(const abstract_item_view_t&) = delete;
        abstract_item_view_t(abstract_item_view_t&&) = delete;
        abstract_item_view_t& operator=(abstract_item_view_t&&) = delete;

        /// @brief The installed model, or nullptr (no model, or the model was destroyed).
        item_model_t* model() const noexcept;

        /// @brief Install the row source; the view does not own it.
        /// @param model The new model, or nullptr to empty the view.
        /// @note Replacing the model disconnects every notification of the old one and resets the
        ///       scroll offset, the current row and the selection. Destroying the model while it is
        ///       installed also disconnects (the guarded pointer turns null and the view resets).
        void set_model(item_model_t* model);

        /// @brief The height shared by every row (the style default until set_row_height()).
        int row_height() const noexcept;

        /// @brief Set the height shared by every row.
        /// @param height The new height; a value below 1 is treated as 1.
        /// @note Changing it keeps the current scroll offset clamped to the new range and repaints the
        ///       body: row boundaries move, so already-painted pixels cannot be shifted.
        void set_row_height(int height);

        /// @brief The current row, or -1 when no row is current.
        int current_row() const noexcept;

        /// @brief Make @p row current, clamped into [-1, row_count() - 1].
        /// @param row The wanted row; -1 clears the current row.
        /// @note In SINGLE mode the selected set becomes exactly the current row. In MULTI mode the
        ///       selected set is left alone (a programmatic current row is not a user selection) and
        ///       the anchor follows the new current row.
        /// @note Does not scroll by itself: use scroll_to_row() to reveal the row.
        void set_current_row(int row);

        /// @brief The selection mode (SINGLE by default).
        selection_mode_enum selection_mode() const noexcept;

        /// @brief Install the selection mode.
        /// @param mode The new mode.
        /// @note Switching to SINGLE collapses the selection to the current row (or empties it when
        ///       there is no current row).
        void set_selection_mode(selection_mode_enum mode);

        /// @brief Whether @p row is in the selected set.
        /// @param row The row to ask about; an out-of-range row is not selected.
        bool is_row_selected(int row) const noexcept;

        /// @brief The selected rows, ascending.
        std::vector<int> selected_rows() const;

        /// @brief Scroll so @p row is fully visible.
        /// @param row The row to reveal; an out-of-range row is ignored.
        /// @note Only the scroll offset changes; the current row and the selection are not touched.
        void scroll_to_row(int row);

        /// @brief The smallest usable layout for a row view.
        /// @return {LIST_VIEW_MIN_WIDTH, row_height() + SCROLL_BAR_THICKNESS}: the style's minimum
        ///         usable width for a row view, and one row plus one scroll bar thickness.
        /// @note The width is a style metric and ignores the rows; the height follows row_height(), so
        ///       set_row_height() changes it. A view whose content needs a wider floor overrides this.
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief Drive hovering, presses, the wheel and the keyboard.
        /// @param in The input snapshot, in this view's local coordinates.
        /// @return true when the view consumed the step.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Event entry point: dispatch the row-level ui events (data / rows change) and defer
        ///        everything else to item_t::event().
        /// @param event The event; nullptr falls through to the base.
        /// @return true when a row-level event was dispatched; otherwise the base's answer.
        /// @note The model's signals build these two events and send them to the view, so the model
        ///       path and the handlers agree on one vocabulary; a subclass overrides the handlers,
        ///       never the connections.
        bool event(::hwshqtb::metaobject::event_t* event) override;

        /// @brief Emitted when the current row actually changed.
        /// @param row The new current row (-1 for none).
        SIGNAL(void current_changed(int row) const)

        /// @brief Emitted when the selected set actually changed.
        SIGNAL(void selection_changed() const)

        /// @brief Emitted when a press was released inside the row it pressed.
        /// @param row The activated row.
        /// @note A release outside the view (or on another row) does not activate anything.
        SIGNAL(void row_activated(int row) const)

    protected:
        /// @brief Paint ONE row: the base draws the background states (selected / current /
        ///        hovered); a subclass adds its row content after calling it.
        /// @param painter The painter, in the row content's coordinates.
        /// @param row The row to paint; its band intersects the destination.
        /// @param rect The row's full content rectangle: content_width() wide and row_height() tall,
        ///        in content coordinates, so a multi-column view lays its cells out here.
        /// @note Called only for a row that intersects the destination, and @p rect is NOT clipped
        ///       to it: a partly visible row is laid out exactly as a fully visible one, which is
        ///       what keeps the scroll fast path's shifted pixels equal to a full repaint.
        virtual void paint_row(::hwshqtb::gui::painter_t& painter, int row,
                               ::hwshqtb::gui::rect_t rect) const;

        /// @brief React to the view item's hover state having changed.
        /// @param event The event; event.hovered() is the new state.
        /// @note The hover feedback belongs to one ROW, not to the whole item: the row rectangles are
        ///       damaged by the input path, so this override does not mark the whole view.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void hover_event(hover_event_t* event) override;

        /// @brief React to the content's extent or origin having changed for a non-input reason (a new
        ///        model, a rows change, a new row height).
        /// @note A subclass that paints decoration outside the content (a table's header) repaints it
        ///       here; the default does nothing.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void content_change_event(content_change_event_t* event) override;

        /// @brief React to rows @p first..@p last of the model having changed their content.
        /// @param event The event; the range is inclusive and is clamped to the visible rows here.
        /// @note A subclass adds whatever else the row's content feeds (a tree's cached content width)
        ///       and keeps the per-row damage this one records.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void data_change_event(data_change_event_t* event);

        /// @brief Re-derive the layout and clamp the row state after a row insert/remove.
        /// @note A subclass whose visible rows are DERIVED must rebuild them before calling this one,
        ///       so the clamp and the scroll ranges see the new row list.
        [[= ::hwshqtb::metaobject::make_hidden()]]
        virtual void rows_change_event(rows_change_event_t* event);

        /// @brief The content extent the vertical and horizontal ranges derive from.
        /// @return {content_width(), row_count * row_height()}: one row's full width and the height of
        ///         every row of the model.
        ::hwshqtb::gui::extent_t content_extent() const override;

        /// @brief Re-derive the hovered row after a scroll moved the content under the pointer.
        void body_scrolled() override;

        /// @brief The full content extent the horizontal scroll range is derived from.
        /// @return The width of one row's content. The default implementation answers the body
        ///         width, so a row view never scrolls horizontally; a table answers the sum of its
        ///         column widths.
        /// @note The horizontal bar is compared against the VIEW's width, not the destination's: a
        ///       visible bar changes the destination, so a content width that IS the destination
        ///       width would otherwise justify a bar that then shrinks the destination below it.
        virtual int content_width() const;

        /// @brief Whether the horizontal bar is needed: the content is wider than the view itself.
        /// @param viewport_width The destination width the visible bars leave.
        /// @return true when content_width() exceeds rect().width().
        /// @note The view's width is the one extent no bar can change, so a row list whose content
        ///       width follows the view never flaps a bar in and out.
        bool horizontal_bar_needed(int viewport_width) const override;

        /// @brief Handle a key this view adds to, or takes over from, the base's row navigation.
        /// @param in The key step; has_focus() and in.key_pressed already hold.
        /// @return true when the view consumed the key, false to let the base handle it.
        /// @note Called BEFORE the base's own keys, so a subclass may claim one of them (a table takes
        ///       Return for cell_activated()) or add new ones (a table's Left/Right move the current
        ///       column). The base's row navigation is untouched for every key the override declines.
        virtual bool handle_extra_key(const ::hwshqtb::gui::interaction_t& in);

        /// @brief How many rows the view shows.
        /// @return The model's row count.
        /// @note A subclass whose rows are a projection of the model — a tree view flattens the
        ///       expanded nodes into the row list it shows — overrides this. Every row operation of
        ///       this class (painting, hit testing, damage, the keyboard, the selection, the stop of
        ///       the scroll range) then works on the rows the view really shows.
        /// @note Read, never cached here: a subclass answers from the state it just rebuilt.
        virtual int visible_row_count() const;

        /// @brief Drop the offsets, the current row, the anchor, the selection and the hovered row.
        /// @note Called when no row identity survives: a new model, a destroyed model, model_reset().
        ///       A subclass overrides it to drop the state it derives from the rows, then calls this
        ///       one so the row-level reset happens exactly as it does for a flat view.
        virtual void reset_contents();

        /// @brief Re-index the current row, the anchor, the hovered row and the selected set after the
        ///        visible rows were rebuilt.
        /// @param row_map For each OLD visible row, the NEW row that holds the same node, or -1 when
        ///        that node is gone.
        /// @note The caller repaints afterwards: the rows moved, so per-row damage is meaningless
        ///       here. current_changed() and selection_changed() are emitted only when the answers
        ///       really changed.
        /// @note A row that @p row_map does not cover is treated as gone, so a map shorter than the
        ///       old row list drops the taller rows.
        void remap_rows(const std::vector<int>& row_map);

        /// @brief The row under @p local, or -1 when the point is outside the body or past the last
        ///        row.
        /// @param local The point in this view's coordinates.
        int row_at(::hwshqtb::gui::point_t local) const;

        /// @brief The content rectangle of @p row in this view's coordinates, not clipped.
        /// @param row The row; the answer of a row outside the visible set is meaningless.
        ::hwshqtb::gui::rect_t row_rect(int row) const;

    private:
        /// @brief The private child item that paints the visible rows and reports the content extent.
        class row_content_t;

        /// @brief The row count of the installed model, or 0 without a model.
        int model_row_count() const;

        /// @brief Connect every notification of the installed model.
        void connect_model();

        /// @brief Disconnect every notification of the installed model and forget it.
        void disconnect_model();

        /// @brief Mark the rows of a data_changed() notification as damaged.
        /// @brief Apply model_reset(): reset the contents and repaint the viewport.
        void handle_model_reset();

        /// @brief React to the model's destruction: forget it and reset the contents.
        void handle_model_destroyed();

        /// @brief Replace the selected set; emits selection_changed() only on a real change.
        void set_selection_rows(std::vector<int> rows);

        /// @brief The inclusive rows between @p first and @p last, in either order.
        std::vector<int> rows_between(int first, int last) const;

        /// @brief Record @p row as the current row and emit current_changed() on a real change.
        /// @param row The already clamped new current row (-1 for none).
        void set_current_row_value(int row);

        /// @brief Move the current row, optionally extending the selection from the anchor.
        /// @param row The wanted row.
        /// @param extend Whether Shift semantics apply (MULTI mode only).
        void move_current(int row, bool extend);

        /// @brief Apply one click at @p row with the modifier bits of the step.
        void click_row(int row, std::uint32_t modifiers);

        /// @brief Clear the current row and the selection (a press outside every row).
        void clear_current_and_selection();

        /// @brief Mark the visible band of @p row as damaged.
        /// @param row The row whose appearance changed; a row outside the model, or one that the
        ///            viewport does not show, damages nothing.
        /// @note Every row-owned visual (hover, selection, current outline) must be repainted through
        ///       this: the row that LOST the state needs the same damage as the one that gained it.
        void damage_row(int row);

        /// @brief The row-change repaint: damage the old and new hovered row bands only.
        void set_hover_row(int row);

        /// @brief Drive the keyboard (the view must hold the focus).
        bool handle_key(const ::hwshqtb::gui::interaction_t& in);

        /// @brief Drive a press: select, take the pointer and remember the pressed row.
        bool handle_press(const ::hwshqtb::gui::interaction_t& in);

        /// @brief Drive a release: activate only when it lands on the pressed row.
        bool handle_release(const ::hwshqtb::gui::interaction_t& in);

        /// @brief Drive the wheel by the style's SCROLL_WHEEL_STEP on both axes.
        bool handle_wheel(const ::hwshqtb::gui::interaction_t& in);

        /// @brief Drive a plain pointer move: update the hovered row.
        bool handle_move(const ::hwshqtb::gui::interaction_t& in);

        /// @brief Paint the rows that intersect the source rectangle, in content coordinates.
        /// @param painter The painter, in content coordinates and clipped to the destination.
        /// @note Called by the private content item; no row item exists per model row.
        void paint_rows(::hwshqtb::gui::painter_t& painter);

        /// @brief The installed model, guarded against destruction elsewhere.
        ::hwshqtb::metaobject::object_pointer_t<item_model_t> _model;
        /// @brief The data_changed() connection, if the model was installed.
        ::hwshqtb::metaobject::connection_handle_t _data_connection;
        /// @brief The rows_inserted() connection, if the model was installed.
        ::hwshqtb::metaobject::connection_handle_t _inserted_connection;
        /// @brief The rows_removed() connection, if the model was installed.
        ::hwshqtb::metaobject::connection_handle_t _removed_connection;
        /// @brief The model_reset() connection, if the model was installed.
        ::hwshqtb::metaobject::connection_handle_t _reset_connection;
        /// @brief The destroyed() connection, if the model was installed.
        ::hwshqtb::metaobject::connection_handle_t _destroyed_connection;

        /// @brief The private content item that paints the rows (owned through the item tree).
        row_content_t* _content_item = nullptr;
        /// @brief The height shared by every row.
        int _row_height = 0;
        /// @brief The current row, or -1.
        int _current_row = -1;
        /// @brief The Shift-selection anchor, or -1 when there is none.
        int _anchor_row = -1;
        /// @brief How many rows may be selected at once.
        selection_mode_enum _selection_mode = selection_mode_enum::SINGLE;
        /// @brief The selected rows, ascending and unique.
        std::vector<int> _selected_rows;
        /// @brief The row the pointer is over, or -1.
        int _hovered_row = -1;
        /// @brief The last pointer position this view was given, in the view's own coordinates.
        /// @note It is the position the hovered row is derived from, so a scroll can re-derive the
        ///       row the pointer is over without waiting for the next pointer step.
        ::hwshqtb::gui::point_t _hover_position{};
        /// @brief Whether @p _hover_position holds a position: false until a pointer step found the
        ///        pointer inside this view, false again once a step finds it outside.
        bool _has_hover_position = false;
        /// @brief The row the current press started on, or -1 when no press is down.
        int _press_row = -1;
    };
}

#endif
