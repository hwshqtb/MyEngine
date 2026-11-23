#include "hwshqtb/ui/grid_layout_t.h"

#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/widget_t.h"

#include "layout_util.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace hwshqtb::ui {
    namespace {
        /// @brief ceil(value / divisor) for value >= 0 and divisor >= 1.
        /// @note Written without value + divisor - 1, which would overflow for unbounded_extent.
        int ceil_div(int value, int divisor) {
            return value / divisor + (value % divisor != 0 ? 1 : 0);
        }
    }

    void grid_layout_t::add(item_t* item, int row, int column, int row_span, int column_span) {
        if (item == nullptr) {
            return;
        }
        entry_t entry;
        entry.item = item;
        entry.row = row > 0 ? row : 0;
        entry.column = column > 0 ? column : 0;
        entry.row_span = row_span > 0 ? row_span : 1;
        entry.column_span = column_span > 0 ? column_span : 1;
        _entries.push_back(entry);
    }

    void grid_layout_t::clear() {
        _entries.clear();
    }

    std::size_t grid_layout_t::count() const {
        return _entries.size();
    }

    ::hwshqtb::gui::extent_t grid_layout_t::size_hint() const {
        return aggregate(false);
    }

    ::hwshqtb::gui::extent_t grid_layout_t::minimum_size() const {
        return aggregate(true);
    }

    ::hwshqtb::gui::extent_t grid_layout_t::aggregate(bool minimum) const {
        int rows = 0;
        int columns = 0;
        for (const entry_t& entry : _entries) {
            ::hwshqtb::metaobject::object_t* const object = entry.item.data();
            if (object == nullptr || !object->is_item()) {
                continue; // a dead entry contributes nothing (apply() prunes it)
            }
            rows = std::max(rows, entry.row + entry.row_span);
            columns = std::max(columns, entry.column + entry.column_span);
        }
        if (rows <= 0 || columns <= 0) {
            return ::hwshqtb::gui::extent_t{ _margins.horizontal(), _margins.vertical() };
        }

        std::vector<int> column_extents(static_cast<std::size_t>(columns), 0);
        std::vector<int> row_extents(static_cast<std::size_t>(rows), 0);
        for (const entry_t& entry : _entries) {
            ::hwshqtb::metaobject::object_t* const object = entry.item.data();
            if (object == nullptr || !object->is_item()) {
                continue;
            }
            const item_t* const item = static_cast<const item_t*>(object);
            // The effective triple, never the bare opinion: a hard constraint has to reach the grid.
            const ::hwshqtb::gui::extent_t extent =
                minimum ? item->effective_minimum_size() : item->effective_size_hint();
            const int extent_width = extent.width() > 0 ? extent.width() : 0;
            const int extent_height = extent.height() > 0 ? extent.height() : 0;
            for (int column = entry.column; column < entry.column + entry.column_span; ++column) {
                const std::size_t index = static_cast<std::size_t>(column);
                column_extents[index] =
                    std::max(column_extents[index], ceil_div(extent_width, entry.column_span));
            }
            for (int row = entry.row; row < entry.row + entry.row_span; ++row) {
                const std::size_t index = static_cast<std::size_t>(row);
                row_extents[index] = std::max(row_extents[index], ceil_div(extent_height, entry.row_span));
            }
        }

        const int gap = _spacing > 0 ? _spacing : 0;
        int width = _margins.horizontal();
        int height = _margins.vertical();
        for (int column = 0; column < columns; ++column) {
            width += column_extents[static_cast<std::size_t>(column)];
        }
        for (int row = 0; row < rows; ++row) {
            height += row_extents[static_cast<std::size_t>(row)];
        }
        if (columns > 1) {
            width += gap * (columns - 1);
        }
        if (rows > 1) {
            height += gap * (rows - 1);
        }
        return ::hwshqtb::gui::extent_t{ width, height };
    }

    void grid_layout_t::apply(widget_t& parent, ::hwshqtb::gui::rect_t area) {
        (void)parent;
        // object_pointer_t nulls with its target: a destroyed item simply loses its cell. A non-item
        // object cannot be placed either (add() only accepts items; this is the defensive check that
        // makes the static_cast below valid).
        std::erase_if(_entries, [](const entry_t& entry) {
            ::hwshqtb::metaobject::object_t* const object = entry.item.data();
            return object == nullptr || !object->is_item();
        });
        if (_entries.empty()) {
            return;
        }

        int rows = 0;
        int columns = 0;
        for (const entry_t& entry : _entries) {
            rows = std::max(rows, entry.row + entry.row_span);
            columns = std::max(columns, entry.column + entry.column_span);
        }
        if (rows <= 0 || columns <= 0) {
            return;
        }

        int content_left = area.left() + _margins.left();
        int content_top = area.top() + _margins.top();
        int content_width = area.width() - _margins.horizontal();
        int content_height = area.height() - _margins.vertical();
        if (content_width < 0) {
            content_width = 0;
        }
        if (content_height < 0) {
            content_height = 0;
        }
        const int gap = _spacing > 0 ? _spacing : 0;

        std::vector<int> column_hints(static_cast<std::size_t>(columns), 0);
        std::vector<int> column_minimums(static_cast<std::size_t>(columns), 0);
        std::vector<int> column_maximums(static_cast<std::size_t>(columns), 0);
        std::vector<int> column_weights(static_cast<std::size_t>(columns), 0);
        std::vector<int> row_hints(static_cast<std::size_t>(rows), 0);
        std::vector<int> row_minimums(static_cast<std::size_t>(rows), 0);
        std::vector<int> row_maximums(static_cast<std::size_t>(rows), 0);
        std::vector<int> row_weights(static_cast<std::size_t>(rows), 0);

        for (const entry_t& entry : _entries) {
            item_t* const item = static_cast<item_t*>(entry.item.data());
            const bool fixed = item->has_fixed_size();
            // The layout asks the effective triple, never size_hint(): the hard constraint wins.
            const ::hwshqtb::gui::extent_t hint = item->effective_size_hint();
            const ::hwshqtb::gui::extent_t minimum = item->effective_minimum_size();
            const ::hwshqtb::gui::extent_t maximum = item->effective_maximum_size();
            const int hint_width = hint.width() > 0 ? hint.width() : 0;
            const int hint_height = hint.height() > 0 ? hint.height() : 0;
            const int minimum_width = minimum.width() > 0 ? minimum.width() : 0;
            const int minimum_height = minimum.height() > 0 ? minimum.height() : 0;
            const int maximum_width = maximum.width() > 0 ? maximum.width() : 0;
            const int maximum_height = maximum.height() > 0 ? maximum.height() : 0;

            for (int column = entry.column; column < entry.column + entry.column_span; ++column) {
                const std::size_t index = static_cast<std::size_t>(column);
                column_hints[index] = std::max(column_hints[index], ceil_div(hint_width, entry.column_span));
                column_minimums[index] =
                    std::max(column_minimums[index], ceil_div(minimum_width, entry.column_span));
                column_maximums[index] =
                    std::max(column_maximums[index], ceil_div(maximum_width, entry.column_span));
            }
            for (int row = entry.row; row < entry.row + entry.row_span; ++row) {
                const std::size_t index = static_cast<std::size_t>(row);
                row_hints[index] = std::max(row_hints[index], ceil_div(hint_height, entry.row_span));
                row_minimums[index] = std::max(row_minimums[index], ceil_div(minimum_height, entry.row_span));
                row_maximums[index] = std::max(row_maximums[index], ceil_div(maximum_height, entry.row_span));
            }

            if (fixed) {
                continue; // a fixed item neither grows nor imposes a growth weight
            }
            const int stretch = item->stretch() > 0 ? item->stretch() : 1;
            if (item->horizontal_policy() == size_policy_enum::EXPANDING) {
                for (int column = entry.column; column < entry.column + entry.column_span; ++column) {
                    const std::size_t index = static_cast<std::size_t>(column);
                    column_weights[index] = std::max(column_weights[index], stretch);
                }
            }
            if (item->vertical_policy() == size_policy_enum::EXPANDING) {
                for (int row = entry.row; row < entry.row + entry.row_span; ++row) {
                    const std::size_t index = static_cast<std::size_t>(row);
                    row_weights[index] = std::max(row_weights[index], stretch);
                }
            }
        }

        for (int column = 0; column < columns; ++column) {
            const std::size_t index = static_cast<std::size_t>(column);
            // Every entry contributes min <= hint <= max to the same line, so the aggregated bounds
            // stay ordered; the max() only guards against a line whose bounds came from two entries.
            column_maximums[index] = std::max(column_maximums[index], column_minimums[index]);
            column_hints[index] = std::max(column_hints[index], column_minimums[index]);
            column_hints[index] = std::min(column_hints[index], column_maximums[index]);
        }
        for (int row = 0; row < rows; ++row) {
            const std::size_t index = static_cast<std::size_t>(row);
            row_maximums[index] = std::max(row_maximums[index], row_minimums[index]);
            row_hints[index] = std::max(row_hints[index], row_minimums[index]);
            row_hints[index] = std::min(row_hints[index], row_maximums[index]);
        }

        std::vector<int> column_sizes(static_cast<std::size_t>(columns), 0);
        std::vector<int> row_sizes(static_cast<std::size_t>(rows), 0);
        detail::resolve_extents(content_width, column_hints, column_minimums, column_maximums,
                                column_weights, gap, column_sizes);
        detail::resolve_extents(content_height, row_hints, row_minimums, row_maximums,
                                row_weights, gap, row_sizes);

        std::vector<int> column_lefts(static_cast<std::size_t>(columns), content_left);
        for (int column = 1; column < columns; ++column) {
            const std::size_t index = static_cast<std::size_t>(column);
            column_lefts[index] = column_lefts[index - 1] + column_sizes[index - 1] + gap;
        }
        std::vector<int> row_tops(static_cast<std::size_t>(rows), content_top);
        for (int row = 1; row < rows; ++row) {
            const std::size_t index = static_cast<std::size_t>(row);
            row_tops[index] = row_tops[index - 1] + row_sizes[index - 1] + gap;
        }

        for (const entry_t& entry : _entries) {
            int cell_width = gap * (entry.column_span - 1);
            for (int column = entry.column; column < entry.column + entry.column_span; ++column) {
                cell_width += column_sizes[static_cast<std::size_t>(column)];
            }
            int cell_height = gap * (entry.row_span - 1);
            for (int row = entry.row; row < entry.row + entry.row_span; ++row) {
                cell_height += row_sizes[static_cast<std::size_t>(row)];
            }
            item_t* const item = static_cast<item_t*>(entry.item.data());
            const ::hwshqtb::gui::rect_t cell{
                column_lefts[static_cast<std::size_t>(entry.column)],
                row_tops[static_cast<std::size_t>(entry.row)],
                cell_width,
                cell_height,
            };
            // The placement of the item inside its cell. Neither branch ever gives the item MORE than
            // the cell here; a hard constraint may still make it larger than a squeezed cell (a fixed
            // size above the union, an overfull axis), and the offsets answer 0 in that case, so
            // alignment never pulls an item out of the cell it was assigned.
            int item_width = cell_width;
            int item_height = cell_height;
            if (item->has_fixed_size()) {
                // The constraint is a hard box on both axes: the item keeps its fixed size instead of
                // stretching to the shared cell, and the alignment bits decide where that box sits when
                // the cell is larger than it.
                const ::hwshqtb::gui::extent_t fixed = item->fixed_size();
                item_width = fixed.width();
                item_height = fixed.height();
            }
            else {
                // A spanning item's cell is the union of several lines, so it can exceed the item's
                // own maximum even though every line was capped: press the placed rectangle down.
                const ::hwshqtb::gui::extent_t maximum = item->effective_maximum_size();
                if (item_width > maximum.width()) {
                    item_width = maximum.width();
                }
                if (item_height > maximum.height()) {
                    item_height = maximum.height();
                }
            }
            const alignment_t alignment = item->alignment();
            const int offset_x = horizontal_alignment_offset(cell_width, item_width, alignment);
            const int offset_y = vertical_alignment_offset(cell_height, item_height, alignment);
            item->set_geometry(::hwshqtb::gui::rect_t{ cell.x() + offset_x, cell.y() + offset_y,
                                                      item_width, item_height });
        }
    }
}
