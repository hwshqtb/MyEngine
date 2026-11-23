#ifndef HWSHQTB__UI__TABLE_VIEW_T_H
#define HWSHQTB__UI__TABLE_VIEW_T_H

/// @file table_view_t.h
/// @brief table_view_t: the multi-column item view — fixed column widths, a header strip and a cell
///        grid over the row machinery of abstract_item_view_t.

#include <vector>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/ui/abstract_item_view_t.h"
#include "hwshqtb/ui/export.h"

namespace hwshqtb::gui {
    class painter_t;
}

namespace hwshqtb::ui {
    /// @brief A table of model cells: columns of fixed width whose headers sit in a strip above the
    ///        rows (the QTableView subset this layer needs).
    ///
    /// @par Rows first, cells second
    /// Everything that makes the view virtualised lives in abstract_item_view_t: the row grid, both
    /// scroll bars, the visible row range, the selection, the keyboard, the model notifications and
    /// per-row damage. This class adds COLUMNS on top: a width per column, the horizontal extent that
    /// drives the inherited horizontal bar, a header strip, a cell grid, and a "current cell" that is
    /// (current row, current column). The selection stays a ROW selection: a click selects the row the
    /// cell belongs to and makes the clicked cell current; there is no per-cell selection set.
    ///
    /// @par The header is a child item
    /// The titles are a private child item of the view, drawn above the content destination. It is
    /// not part of the content: it neither scrolls vertically nor enlarges the content extent, and
    /// the destination (and therefore the scroll ranges) is the body minus its height. Hiding the
    /// header repaints no titles and leaves the destination unchanged.
    ///
    /// @par Virtualisation
    /// A paint pass asks the model only for the cells inside the viewport: the visible row range comes
    /// from the base, the visible column range from the horizontal offset and the viewport width. The
    /// cost of a frame is therefore independent of the model's row AND column count.
    ///
    /// @par Column widths
    /// A column is as wide as set_column_width() made it, or, when no width was installed, as wide as
    /// the style's TABLE_COLUMN_WIDTH. content_width() — the width all columns add up to — is what the
    /// inherited horizontal bar scrolls: an over-wide table gets the bar, a narrow one does not.
    ///
    /// @par Deliberately absent
    /// No per-cell selection or editing, no variable row height, no column dragging/resizing by the
    /// pointer (widths are programmatic), no sorting/filtering and no horizontal auto-scroll that
    /// follows the current column.
    class HWSHQTB_UI_EXPORT table_view_t: public abstract_item_view_t {
        OBJECT

    public:
        /// @brief Construct an empty table with no model, no current cell and no selection.
        /// @param parent The visual parent; nullptr makes the table a root widget.
        /// @note The header strip always occupies the top of the view, even without a model.
        explicit table_view_t(widget_t* parent = nullptr);

        table_view_t(const table_view_t&) = delete;
        table_view_t& operator=(const table_view_t&) = delete;
        table_view_t(table_view_t&&) = delete;
        table_view_t& operator=(table_view_t&&) = delete;

        /// @brief The width of @p column: the installed value, else the style's TABLE_COLUMN_WIDTH.
        /// @param column The column index; a negative index answers 0.
        /// @return The width the column has (or would have: the answer does not check the model's
        ///         column count, so a width installed before set_model() is already reported).
        int column_width(int column) const;

        /// @brief Set the fixed width of @p column.
        /// @param column The column index; a negative index is ignored.
        /// @param width The new width in pixels; a negative value is treated as 0 (a hidden column).
        /// @note The width is remembered even when the model has no such column yet, so columns may be
        ///       configured before set_model(). Changing a column of a visible table re-derives the
        ///       horizontal range and repaints the view.
        void set_column_width(int column, int width);

        /// @brief The left edge of @p column in content coordinates.
        /// @param column The column index.
        /// @return The sum of the widths of the model's columns before @p column. A negative index
        ///         answers 0, and an index at or past column_count() answers content_width().
        int column_left(int column) const;

        /// @brief The current column, or -1 when there is no current cell (or no column at all).
        /// @return The recorded column, clamped into the model's current column count.
        /// @note The current column lives with the current row: when the base clears the current row
        ///       (a reset, a destroyed model, a press outside every row) the column is cleared too.
        ///       The inherited set_current_row() does not touch it — use set_current_cell().
        int current_column() const;

        /// @brief Make the cell (@p row, @p column) the current cell.
        /// @param row The row; a negative row clears the current cell.
        /// @param column The column, clamped into [0, column_count() - 1]; a negative value means the
        ///        first column.
        /// @note The row half goes through the inherited set_current_row(), so the selection follows
        ///       it exactly as it does for a list (SINGLE: the row becomes the selection).
        /// @note Does not scroll: use the inherited scroll_to_row() to reveal the row. The horizontal
        ///       offset is not moved either.
        void set_current_cell(int row, int column);

        /// @brief The column under @p local, or -1 when the point is not inside a cell.
        /// @param local The point in this view's coordinates.
        /// @return The column whose half-open band contains the point; a point exactly on a column
        ///         boundary belongs to the column on its right. The header strip, both scroll bars,
        ///         the area right of the last column and everything outside the view answer -1.
        int column_at(::hwshqtb::gui::point_t local) const;

        /// @brief The natural size: the column widths, capped, and the list's default height.
        /// @return {min(content_width(), TABLE_MAX_WIDTH), LIST_VIEW_HEIGHT}.
        /// @note The hint ignores the row count, exactly like the list's: the rows only move the
        ///       vertical scroll range.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest usable layout for a table.
        /// @return {column_width(0) + 2 * SCROLL_BAR_THICKNESS, header height + row_height() +
        ///         2 * SCROLL_BAR_THICKNESS}: one column plus room for both bars, and one row under
        ///         the header plus room for both bars.
        ::hwshqtb::gui::extent_t minimum_size_hint() const override;

        /// @brief The width all model columns add up to: the table's content extent.
        /// @return sum(column_width(column)) for the model's columns; 0 without a model.
        /// @note This is the extent the inherited horizontal bar scrolls: the bar appears exactly when
        ///       it exceeds the view's own width.
        int content_width() const override;

        /// @brief The header strip item (a private child of this view; never nullptr).
        /// @note Hiding it stops the titles from being drawn; the destination and the scroll ranges
        ///       stay as they are, because the strip is not part of the content.
        widget_t* header_item() const noexcept;

        /// @brief Drive the inherited input, and remember which column a press landed on.
        /// @param in The input snapshot, in this view's local coordinates.
        /// @return true when the view consumed the step.
        /// @note A press selects the ROW through the inherited logic; this override only records the
        ///       column of that press as the current one.
        /// @note A press inside the header strip is consumed without touching the current cell or the
        ///       selection and without taking the pointer, so the release that follows is not claimed
        ///       by the view.
        bool interact(const ::hwshqtb::gui::interaction_t& in) override;

        /// @brief Emitted when the user activates a cell with Return.
        /// @param row The current row.
        /// @param column The current column.
        /// @note Emitted only when both halves of the current cell exist. Return does NOT also emit
        ///       the inherited row_activated(): the table's activation is the cell.
        SIGNAL(void cell_activated(int row, int column) const)

    protected:
        /// @brief Paint the visible cells of ONE row: the grid line of each column, then its text.
        /// @param painter The painter, in the row content's coordinates and clipped to the row area.
        /// @param row The row to paint.
        /// @param rect The row's full content rectangle; its x/y are content coordinates.
        /// @note Only the cells inside the destination are asked of the model. Text is clipped to its
        ///       cell, so a long value never bleeds into the neighbouring column.
        void paint_row(::hwshqtb::gui::painter_t& painter, int row,
                       ::hwshqtb::gui::rect_t rect) const override;

        /// @brief Repaint the titles after the model or the row grid changed.
        /// @param event The content event (no payload: this view re-reads the model and the grid).
        [[= ::hwshqtb::metaobject::make_hidden()]]
        void content_change_event(content_change_event_t* event) override;

        /// @brief Reserve the header band: the content is shown below it.
        /// @param body body_rect() for the current bars.
        /// @return The body minus the strip the header item occupies.
        /// @note This is the one place the header influences the scroll geometry; the header item
        ///       itself is not part of the content and is painted by the child traversal.
        ::hwshqtb::gui::rect_t destination_for_body(::hwshqtb::gui::rect_t body) const override;

        /// @brief Re-place the header item and keep the destination current.
        void update_layout() override;

        /// @brief Repaint the header when the horizontal offset moved it.
        void body_scrolled() override;

        /// @brief Handle Left/Right (move the current column) and Return (activate the cell).
        /// @param in The key step.
        /// @return true for those keys, false for every other key (the base's row navigation).
        bool handle_extra_key(const ::hwshqtb::gui::interaction_t& in) override;

    private:
        /// @brief The private child item that paints the header strip.
        class header_item_t;

        /// @brief Paint the header strip and the title of every visible column, in the strip's own
        ///        coordinates.
        void paint_header(::hwshqtb::gui::painter_t& painter) const;

        /// @brief Mark the header strip as damaged (it is outside the shifted content).
        void damage_header();

        /// @brief The column count of the installed model, or 0 without a model.
        int column_count() const;

        /// @brief The first column whose band reaches the viewport, or column_count() when none does.
        int first_visible_column() const;

        /// @brief The last column whose band reaches the viewport, or -1 when none does.
        int last_visible_column() const;

        /// @brief Record @p column as the current one; damages the current row on a change.
        void set_current_column(int column);

        /// @brief Damage the visible band of the current row (the current-cell outline moved).
        void update_current_row();

        /// @brief The widths installed by set_column_width(), indexed by column; -1 means "not set".
        /// @note It grows on demand and is deliberately NOT shrunk by a model change: a width
        ///       installed for a column the new model does not have is remembered for the next one.
        std::vector<int> _column_widths;
        /// @brief The current column, or -1; only meaningful together with the inherited current row.
        int _current_column = -1;
        /// @brief The private header item (owned through the item tree; never nullptr).
        header_item_t* _header_item = nullptr;
    };
}

#endif
