#ifndef HWSHQTB__UI__GRID_LAYOUT_T_H
#define HWSHQTB__UI__GRID_LAYOUT_T_H

/// @file grid_layout_t.h
/// @brief grid_layout_t: a row/column layout with spans (stage W2).

#include <cstddef>
#include <vector>

#include "hwshqtb/metaobject/object_pointer_t.h"
#include "hwshqtb/ui/layout_t.h"

namespace hwshqtb::ui {
    class item_t;
    class widget_t;

    /// @brief A grid layout: every added item occupies one cell, optionally spanning rows/columns
    ///        (the QGridLayout algorithm, minimal form).
    ///
    /// @par Column and row sizing
    /// 1. the content area is the layout area minus margins();
    /// 2. a column's hint is the largest EFFECTIVE hint of the items in it (an item spanning N
    ///    columns contributes ceil(hint / N) to each of them); the same rule gives the row hints;
    /// 3. a column/row grows when at least one of its items asks for size_policy_enum::EXPANDING on
    ///    that axis; its weight is the largest stretch() among them (a zero stretch counts as 1).
    ///    A FIXED item contributes its constraint, never grows and adds no weight. The remainder is
    ///    distributed in proportion to those weights, never above the line's effective maximum (the
    ///    largest effective_maximum_size() of its items, divided over a span); pixels a line cannot
    ///    take go to the remaining growing lines;
    /// 4. an overfull axis shrinks proportionally to the room above the effective minimum sizes,
    ///    exactly like box_layout_t (a line whose floor already reaches its hint does not shrink);
    ///    when even the floors do not fit, the biggest lines are cut first, below their floor if
    ///    necessary, so the axis still fits (never negative);
    /// 5. spacing() separates neighbouring columns/rows and margins() frame the whole grid.
    ///
    /// @par Cell rectangles
    /// A spanning item receives the union of its columns/rows (including the spacing between them),
    /// so the grid looks continuous. A FIXED item keeps its fixed box instead of stretching to a
    /// shared cell that other items made larger. A non-fixed item stretches to its cell but is pressed
    /// down to its effective_maximum_size() when the union or a shared line is larger. An item keeps its
    /// cell assignment even if it is later re-parented: adding an item does not adopt it, it only maps
    /// it to a cell.
    ///
    /// @par Alignment
    /// item_t::alignment() places the item inside its cell when the cell is larger than the item, on
    /// both axes independently: a shared line (or a union of several lines) can be bigger than the
    /// item's maximum, and a fixed item is smaller than its cell almost by construction. An axis
    /// without a bit keeps the cell origin, and a cell no larger than the item (an overfull axis, a
    /// fixed size above the union) ignores the bits. alignment_flag::NONE is the default, so an
    /// unaligned grid lays out exactly as before.
    ///
    /// @par Lifetime
    /// Entries hold an object_pointer_t, so destroying an added item simply removes its cell on the
    /// next apply() instead of leaving a dangling entry behind.
    class HWSHQTB_UI_EXPORT grid_layout_t final : public layout_t {
    public:
        /// @brief Construct an empty grid.
        grid_layout_t() = default;

        /// @brief Put @p item into the cell (@p row, @p column), optionally spanning cells.
        /// @param item The item to place; nullptr is ignored.
        /// @param row The zero-based row; a negative value is treated as 0.
        /// @param column The zero-based column; a negative value is treated as 0.
        /// @param row_span How many rows the item covers; values below 1 are treated as 1.
        /// @param column_span How many columns the item covers; values below 1 are treated as 1.
        /// @note Adding the same item twice maps it to two cells (the later entry wins on apply,
        ///       because it is positioned last); callers normally add an item once.
        void add(item_t* item, int row, int column, int row_span = 1, int column_span = 1);

        /// @brief Forget every entry (the items themselves are untouched).
        void clear();

        /// @brief The number of recorded entries (entries of destroyed items are pruned on apply()).
        std::size_t count() const;

        /// @brief Lay the entries out; entries whose item is gone are dropped first.
        /// @param parent The widget whose client rect supplies the area (the entries need not be its
        ///        children, but they normally are).
        /// @param area The parent's client rect.
        void apply(widget_t& parent, ::hwshqtb::gui::rect_t area) override;

        /// @brief The natural size of the container once laid out by this grid.
        /// @return The sum of the per-column effective hints plus the spacing between them plus the
        ///         horizontal margins (the same for rows/height). A cell's hint is the largest
        ///         effective hint of the items in it; a spanning item contributes ceil(hint / span)
        ///         to each covered line, exactly as apply() sizes it. An empty grid answers the
        ///         margins' sum.
        ::hwshqtb::gui::extent_t size_hint() const override;

        /// @brief The smallest size this grid can arrange its items at (QLayout::minimumSize).
        /// @return The same composition as size_hint() with each item's effective_minimum_size() in
        ///         place of its effective hint: the per-column minima summed plus the spacing and the
        ///         horizontal margins (the same for rows/height). A spanning item contributes
        ///         ceil(minimum / span) to each covered line, exactly as apply() floors it.
        ::hwshqtb::gui::extent_t minimum_size() const override;

    private:
        /// @brief One cell assignment; the guarded pointer nulls when the item is destroyed.
        struct entry_t {
            ::hwshqtb::metaobject::object_pointer_t<::hwshqtb::metaobject::object_t> item;
            int row = 0;
            int column = 0;
            int row_span = 1;
            int column_span = 1;
        };

        /// @brief The shared body of size_hint() and minimum_size().
        /// @param minimum true aggregates effective_minimum_size(), false effective_size_hint().
        ::hwshqtb::gui::extent_t aggregate(bool minimum) const;

        /// @brief The cell assignments in the order they were added.
        std::vector<entry_t> _entries;
    };
}

#endif
