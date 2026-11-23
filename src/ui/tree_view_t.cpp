/// @file tree_view_t.cpp
/// @brief tree_view_t: the flattened visible list, the expansion state, the row painting (indentation,
///        expander, text) and the tree's key and pointer contract (see tree_view_t.h).

#include "hwshqtb/ui/tree_view_t.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    namespace {
        /// @brief The text a model answered for a role: the view itself when the any_t holds one, or a
        ///        copy in @p owned when it holds a std::string.
        /// @param value The value the model answered.
        /// @param owned Storage for a std::string payload; the caller keeps it alive while drawing.
        /// @return The text to draw; empty when the value holds no string at all.
        std::string_view text_of(const ::hwshqtb::metaobject::any_t& value, std::string& owned) {
            if (!value.is_initialized()) {
                return {};
            }
            if (value.is<std::string_view>()) {
                return value.get<std::string_view>();
            }
            if (value.is<std::string>()) {
                owned = value.get<std::string>();
                return owned;
            }
            return {};
        }

        /// @brief The style's horizontal text padding: the row's leading inset and the gap between the
        ///        expander and the text.
        int text_padding() {
            return std::max(0, default_style().pixel_metric(pixel_metric_enum::TEXT_PADDING));
        }

        /// @brief The edge length of a row's expander.
        int expander_size() {
            return std::max(0, default_style().pixel_metric(pixel_metric_enum::TREE_EXPANDER_SIZE));
        }

        /// @brief Whether @p left and @p right name the same node.
        /// @return true when both carry the same model node (and column), or — for a model that leaves
        ///         the node null — when both name the same (row, column).
        /// @note This is the identity index_less_t orders by, not the raw triple: a node keeps its
        ///       state when a sibling inserted in front of it moves its row field.
        bool same_node(const item_index_t& left, const item_index_t& right) {
            if (left.node != nullptr || right.node != nullptr) {
                return left.node == right.node && left.column == right.column;
            }
            return left.row == right.row && left.column == right.column;
        }
    }

    bool tree_view_t::index_less_t::operator()(const item_index_t& left,
                                               const item_index_t& right) const noexcept {
        // The key is the node's IDENTITY, not the raw triple: a node keeps its state when a sibling is
        // inserted in front of it (its row field moves), while a model that leaves the node null gets
        // the (row, column) pair item_index_t documents as its identity. The two kinds never compare
        // equal, which is what keeps the order a strict weak ordering.
        const bool left_has_node = left.node != nullptr;
        const bool right_has_node = right.node != nullptr;
        if (left_has_node != right_has_node) {
            return left_has_node;
        }
        if (left_has_node) {
            // std::less gives the total order the built-in relational operators do not promise for
            // pointers into unrelated objects.
            const std::less<const void*> before;
            if (before(left.node, right.node)) {
                return true;
            }
            if (before(right.node, left.node)) {
                return false;
            }
            return left.column < right.column;
        }
        if (left.row != right.row) {
            return left.row < right.row;
        }
        return left.column < right.column;
    }

    tree_view_t::tree_view_t(widget_t* parent):
        abstract_item_view_t(parent) {
        _indentation = std::max(0, default_style().pixel_metric(pixel_metric_enum::TREE_INDENTATION));
    }

    void tree_view_t::set_indentation(int pixels) {
        const int next = std::max(0, pixels);
        if (next == _indentation) {
            return;
        }
        _indentation = next;
        recompute_widest_row(); // the indentation is part of every row's extent
        refresh_rows();         // every visible row moved: the whole destination is repainted
    }

    int tree_view_t::indentation() const noexcept {
        return _indentation;
    }

    bool tree_view_t::is_expandable(const item_index_t& index) const {
        const item_model_t* const source = model();
        if (source == nullptr || !index.is_valid()) {
            return false;
        }
        return source->row_count(index) > 0;
    }

    bool tree_view_t::is_expanded(const item_index_t& index) const {
        if (!index.is_valid()) {
            return false;
        }
        return _expanded_nodes.find(index) != _expanded_nodes.end();
    }

    void tree_view_t::expand(const item_index_t& index) {
        if (!is_expandable(index) || is_expanded(index) || visible_row_of(index) < 0) {
            return; // nothing to show, already shown, or the node is not on screen to show it at
        }
        _expanded_nodes.insert(index);
        rebuild_visible_rows();
        refresh_rows();
        expanded_changed(index, true);
    }

    void tree_view_t::collapse(const item_index_t& index) {
        if (_expanded_nodes.erase(index) == 0) {
            return; // only a real state change rebuilds and announces itself
        }
        rebuild_visible_rows();
        refresh_rows();
        expanded_changed(index, false);
    }

    void tree_view_t::toggle_expanded(const item_index_t& index) {
        if (is_expanded(index)) {
            collapse(index);
        }
        else {
            expand(index);
        }
    }

    item_index_t tree_view_t::current_index() const {
        const int row = current_row();
        if (row < 0 || static_cast<std::size_t>(row) >= _rows.size()) {
            return item_index_t{};
        }
        return _rows[static_cast<std::size_t>(row)].index;
    }

    void tree_view_t::set_current_index(const item_index_t& index) {
        const int row = visible_row_of(index);
        if (row < 0) {
            return; // not one of the visible rows: the caller reveals its ancestors first
        }
        // The inherited setter owns the clamping, the anchor and the selection rule of the mode.
        set_current_row(row);
    }

    item_index_t tree_view_t::index_at(::hwshqtb::gui::point_t local) const {
        const int row = row_at(local);
        if (row < 0 || static_cast<std::size_t>(row) >= _rows.size()) {
            return item_index_t{};
        }
        return _rows[static_cast<std::size_t>(row)].index;
    }

    int tree_view_t::visible_row_count() const {
        return static_cast<int>(_rows.size());
    }

    ::hwshqtb::gui::extent_t tree_view_t::size_hint() const {
        const style_t& style = default_style();
        return ::hwshqtb::gui::extent_t{
            style.pixel_metric(pixel_metric_enum::LIST_VIEW_WIDTH),
            style.pixel_metric(pixel_metric_enum::LIST_VIEW_HEIGHT),
        };
    }

    int tree_view_t::content_width() const {
        // The cached part is the widest row; the body width is added here rather than cached, because a
        // resize changes the space the rows must fill without touching a single row.
        return std::max(_widest_row, body_rect().width());
    }

    void tree_view_t::paint_row(::hwshqtb::gui::painter_t& painter, int row,
                                ::hwshqtb::gui::rect_t rect) const {
        abstract_item_view_t::paint_row(painter, row, rect); // the row's background states
        if (row < 0 || static_cast<std::size_t>(row) >= _rows.size()) {
            return;
        }
        const row_info_t& info = _rows[static_cast<std::size_t>(row)];
        const int size = expander_size();
        const int padding = text_padding();
        // The expander column starts at the row's leading inset and the text one padding after it, so
        // a leaf's text lines up with the text of an expandable sibling.
        const int column_left = rect.x() + padding + info.depth * _indentation;
        control_state_t state;
        state.enabled = is_enabled();
        if (info.expandable && size > 0) {
            state.checked = is_expanded(info.index);
            const ::hwshqtb::gui::rect_t expander{ column_left,
                                                   rect.y() + (rect.height() - size) / 2, size, size };
            const ::hwshqtb::gui::rect_t visible = expander.intersected(rect);
            if (!visible.isEmpty()) {
                painter.save();
                painter.set_clip(visible);
                default_style().draw_control(control_element_enum::TREE_EXPANDER, painter, expander,
                                             state);
                painter.restore();
            }
        }
        const item_model_t* const source = model();
        if (source == nullptr) {
            return;
        }
        std::string owned;
        const std::string_view text =
            text_of(source->data(info.index, item_role_enum::DISPLAY), owned);
        if (text.empty()) {
            return;
        }
        const int text_left = column_left + size + padding;
        const ::hwshqtb::gui::rect_t text_rect{ text_left, rect.y(),
                                                std::max(0, rect.right() - text_left),
                                                rect.height() };
        const ::hwshqtb::gui::rect_t visible = text_rect.intersected(rect);
        if (visible.isEmpty()) {
            return;
        }
        state.text = text;
        painter.save();
        painter.set_clip(visible); // a long text is cut at the row's content edge
        default_style().draw_control(control_element_enum::LIST_TEXT, painter, text_rect, state);
        painter.restore();
    }

    bool tree_view_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        if (!is_enabled()) {
            return abstract_item_view_t::interact(in);
        }
        const bool key_step = in.key != 0 || !in.text_input.empty();
        if (key_step || !in.mouse_pressed) {
            return abstract_item_view_t::interact(in);
        }
        update_layout(); // the expander sits in the destination the current bars leave
        const int row = expander_row_at(in.mouse);
        if (row >= 0) {
            // The expander belongs to the NODE, not to the row: the press toggles it, leaves the
            // current row and the selection alone and takes no pointer capture, so the release that
            // follows cannot activate a row either.
            toggle_expanded(_rows[static_cast<std::size_t>(row)].index);
            return true;
        }
        return abstract_item_view_t::interact(in);
    }

    bool tree_view_t::handle_extra_key(const ::hwshqtb::gui::interaction_t& in) {
        if (in.key != ::hwshqtb::gui::interaction_t::left_key
            && in.key != ::hwshqtb::gui::interaction_t::right_key) {
            return false;
        }
        const int row = current_row();
        if (row < 0 || static_cast<std::size_t>(row) >= _rows.size()) {
            return false; // no current node: the inherited navigation owns the arrows
        }
        const item_index_t index = _rows[static_cast<std::size_t>(row)].index;
        if (in.key == ::hwshqtb::gui::interaction_t::right_key) {
            if (is_expandable(index) && !is_expanded(index)) {
                expand(index); // the current row shows the node, so it does not move
                return true;
            }
            if (is_expanded(index) && row + 1 < visible_row_count()) {
                move_to_row(row + 1); // an expanded node's first child is the very next visible row
            }
            return true;
        }
        if (is_expanded(index)) {
            collapse(index);
            return true;
        }
        const item_model_t* const source = model();
        if (source != nullptr) {
            const int parent_row = visible_row_of(source->parent(index));
            if (parent_row >= 0) {
                move_to_row(parent_row);
            }
        }
        return true;
    }

    void tree_view_t::reset_contents() {
        _expanded_nodes.clear();
        _rows.clear();
        flatten(item_index_t{}, 0); // a new model starts fully collapsed
        recompute_widest_row();
        abstract_item_view_t::reset_contents(); // drops the current row, the selection and the offsets
    }

    void tree_view_t::data_change_event(data_change_event_t* event) {
        int first = event->first();
        int last = event->last();
        const int count = visible_row_count();
        if (count <= 0) {
            return;
        }
        first = std::max(0, first);
        last = std::min(count - 1, last);
        if (first > last) {
            return;
        }
        const int before = content_width();
        for (int row = first; row <= last; ++row) {
            row_info_t& info = _rows[static_cast<std::size_t>(row)];
            info.text_width = measure_text(info.index);
        }
        recompute_widest_row();
        if (content_width() != before) {
            // The widest row moved the horizontal range, and with it the position of every row: the
            // per-row bands of the base cannot describe that, so the whole destination is repainted.
            update_layout();
            update(destination_rect());
            return;
        }
        abstract_item_view_t::data_change_event(event); // exactly the changed rows
    }

    void tree_view_t::rows_change_event(rows_change_event_t* event) {
        rebuild_visible_rows(); // the rows the view shows come from the model that just changed
        abstract_item_view_t::rows_change_event(event); // clamps, re-derives the layout, repaints
    }

    int tree_view_t::visible_row_of(const item_index_t& index) const {
        if (!index.is_valid()) {
            return -1;
        }
        for (std::size_t row = 0; row < _rows.size(); ++row) {
            if (same_node(_rows[row].index, index)) {
                return static_cast<int>(row);
            }
        }
        return -1;
    }

    ::hwshqtb::gui::rect_t tree_view_t::expander_rect(int row) const {
        if (row < 0 || static_cast<std::size_t>(row) >= _rows.size()) {
            return ::hwshqtb::gui::rect_t{};
        }
        const row_info_t& info = _rows[static_cast<std::size_t>(row)];
        const int size = expander_size();
        if (!info.expandable || size <= 0) {
            return ::hwshqtb::gui::rect_t{};
        }
        const int height = row_height();
        return ::hwshqtb::gui::rect_t{ text_padding() + info.depth * _indentation,
                                       row * height + (height - size) / 2, size, size };
    }

    int tree_view_t::expander_row_at(::hwshqtb::gui::point_t local) const {
        const int row = row_at(local);
        if (row < 0) {
            return -1;
        }
        const ::hwshqtb::gui::rect_t expander = expander_rect(row);
        if (expander.isEmpty()) {
            return -1;
        }
        // The point in CONTENT coordinates: the destination shows the content shifted by both offsets.
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        const ::hwshqtb::gui::point_t content{ local.x() - destination.x() + horizontal_offset(),
                                              local.y() - destination.y() + vertical_offset() };
        return expander.contains(content) ? row : -1;
    }

    int tree_view_t::row_extent(const row_info_t& row) const {
        const int padding = text_padding();
        // The expander column is reserved for every row, so the leaves line up with their siblings.
        const int text_left = padding + row.depth * _indentation + expander_size() + padding;
        return text_left + row.text_width + padding;
    }

    int tree_view_t::measure_text(const item_index_t& index) const {
        const item_model_t* const source = model();
        if (source == nullptr || !index.is_valid()) {
            return 0;
        }
        std::string owned;
        const std::string_view text =
            text_of(source->data(index, item_role_enum::DISPLAY), owned);
        if (text.empty()) {
            return 0;
        }
        const ::hwshqtb::gui::font_metrics_t metrics{};
        return metrics.text_extent(text).width();
    }

    void tree_view_t::flatten(const item_index_t& parent, int depth) {
        const item_model_t* const source = model();
        if (source == nullptr) {
            return;
        }
        const int count =
            std::max(0, parent.is_valid() ? source->row_count(parent) : source->row_count());
        for (int row = 0; row < count; ++row) {
            const item_index_t index = source->index(row, 0, parent);
            if (!index.is_valid()) {
                continue; // the model declined to name the row: nothing to show for it
            }
            row_info_t info;
            info.index = index;
            info.depth = depth;
            info.expandable = is_expandable(index);
            info.text_width = measure_text(index);
            _rows.push_back(info);
            if (info.expandable && is_expanded(index)) {
                flatten(index, depth + 1);
            }
        }
    }

    void tree_view_t::recompute_widest_row() {
        int widest = 0;
        for (const row_info_t& row : _rows) {
            widest = std::max(widest, row_extent(row));
        }
        _widest_row = widest;
    }

    void tree_view_t::rebuild_visible_rows() {
        std::vector<item_index_t> previous;
        previous.reserve(_rows.size());
        for (const row_info_t& row : _rows) {
            previous.push_back(row.index);
        }
        _rows.clear();
        flatten(item_index_t{}, 0);
        recompute_widest_row();
        // Row numbers are indices into the NEW list, so every row-owned piece of state (the current
        // row, the anchor, the hover, the selected set) is re-mapped by the node it held. A node whose
        // identity is known is matched by that identity; a flat model's rows (node == nullptr) have
        // none, so they are matched by their (row, column) pair — the identity item_index_t documents
        // for a model that leaves the node null.
        std::map<const void*, int> by_node;
        std::map<std::pair<int, int>, int> by_cell;
        for (std::size_t row = 0; row < _rows.size(); ++row) {
            const item_index_t& index = _rows[row].index;
            if (index.node != nullptr) {
                by_node.emplace(index.node, static_cast<int>(row));
            }
            else {
                by_cell.emplace(std::make_pair(index.row, index.column), static_cast<int>(row));
            }
        }
        std::vector<int> row_map(previous.size(), -1);
        for (std::size_t row = 0; row < previous.size(); ++row) {
            const item_index_t& index = previous[row];
            if (index.node != nullptr) {
                const auto found = by_node.find(index.node);
                if (found != by_node.end()) {
                    row_map[row] = found->second;
                }
            }
            else {
                const auto found = by_cell.find(std::make_pair(index.row, index.column));
                if (found != by_cell.end()) {
                    row_map[row] = found->second;
                }
            }
        }
        remap_rows(row_map);
    }

    void tree_view_t::refresh_rows() {
        update_layout(); // the row count, the bars and the ranges all moved
        update(destination_rect());
    }

    void tree_view_t::move_to_row(int row) {
        set_current_row(row);
        scroll_to_row(row);
    }

    META_OBJECT(tree_view_t);
    META_REGISTER_WIDGET(tree_view_t);
}
