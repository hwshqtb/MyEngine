/// @file table_view_t.cpp
/// @brief table_view_t: the column layout, the header child item, the virtualised cell painting and
///        the cell navigation on top of the row machinery of abstract_item_view_t (see
///        table_view_t.h).

#include "hwshqtb/ui/table_view_t.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

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

        /// @brief The header strip's height: the style's TABLE_HEADER_HEIGHT, never negative.
        int header_height() {
            return std::max(0,
                            default_style().pixel_metric(pixel_metric_enum::TABLE_HEADER_HEIGHT));
        }
    }

    /// @brief The private header item: paints the column titles in its own coordinates.
    /// @note It is a child item of the view, so it is painted after the content and above it, and the
    ///       router can hit it. Its height is the strip the destination reserves; hiding it stops the
    ///       titles from being drawn without moving the content.
    class table_view_t::header_item_t final: public widget_t {
    public:
        /// @brief Construct the header item of @p view.
        /// @param view The owning view; it must outlive this item.
        explicit header_item_t(table_view_t* view):
            widget_t(view),
            _view(view) { }

        /// @brief The strip has no size opinion of its own: the view gives it its geometry.
        ::hwshqtb::gui::extent_t size_hint() const override {
            return ::hwshqtb::gui::extent_t{};
        }

        /// @brief The strip has no smaller usable size of its own.
        ::hwshqtb::gui::extent_t minimum_size_hint() const override {
            return ::hwshqtb::gui::extent_t{};
        }

    protected:
        /// @brief Paint the strip and its titles.
        /// @param painter The painter, already translated to this item and clipped to its rect().
        void paint_event(paint_event_t* event) override {
            _view->paint_header(event->painter());
        }

    private:
        /// @brief The owning view (not owned).
        table_view_t* _view = nullptr;
    };

    table_view_t::table_view_t(widget_t* parent):
        abstract_item_view_t(parent) {
        _header_item = new header_item_t{ this };
        // The current column lives with the current row: every path that clears the row (a model
        // reset, a destroyed model, a press outside every row) announces it through current_changed(),
        // so the cell is dropped where the row is dropped instead of leaving a column behind.
        connect("current_changed(int) const", [this](int current) {
            if (current < 0) {
                set_current_column(-1);
            }
        });
        update_layout();
    }

    widget_t* table_view_t::header_item() const noexcept {
        return _header_item;
    }

    void table_view_t::set_column_width(int column, int width) {
        if (column < 0) {
            return;
        }
        const std::size_t index = static_cast<std::size_t>(column);
        if (_column_widths.size() <= index) {
            _column_widths.resize(index + 1, -1);
        }
        const int next = std::max(0, width);
        if (_column_widths[index] == next) {
            return;
        }
        _column_widths[index] = next;
        update_layout(); // the content width moved: the horizontal range and its bar follow
        update(rect());  // every visible cell's position changed
    }

    int table_view_t::column_width(int column) const {
        if (column < 0) {
            return 0;
        }
        const std::size_t index = static_cast<std::size_t>(column);
        if (index < _column_widths.size() && _column_widths[index] >= 0) {
            return _column_widths[index];
        }
        return default_style().pixel_metric(pixel_metric_enum::TABLE_COLUMN_WIDTH);
    }

    int table_view_t::column_left(int column) const {
        const int count = column_count();
        const int last = std::min(column, count);
        int left = 0;
        for (int index = 0; index < last; ++index) {
            left += column_width(index);
        }
        return left;
    }

    int table_view_t::current_column() const {
        if (_current_column < 0) {
            return -1;
        }
        const int count = column_count();
        if (count <= 0) {
            return -1;
        }
        // The model's column count may have shrunk under the recorded column: the answer never names
        // a column the model does not have.
        return std::min(_current_column, count - 1);
    }

    void table_view_t::set_current_cell(int row, int column) {
        const int count = column_count();
        if (row < 0 || count <= 0) {
            set_current_row(-1);
            set_current_column(-1);
            return;
        }
        set_current_row(row); // the base clamps the row and owns the selection and the row outline
        if (current_row() < 0) {
            set_current_column(-1); // the model has no rows
            return;
        }
        set_current_column(std::clamp(column, 0, count - 1));
    }

    int table_view_t::column_at(::hwshqtb::gui::point_t local) const {
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        if (!destination.contains(local)) {
            return -1; // the header band, a scroll bar or the outside of the view
        }
        const int count = column_count();
        if (count <= 0) {
            return -1;
        }
        const int content_x = local.x() - destination.x() + scroll_offset().x();
        int left = 0;
        for (int column = 0; column < count; ++column) {
            const int next = left + column_width(column);
            if (content_x < next) {
                return column; // the right edge of a column belongs to the next one
            }
            left = next;
        }
        return -1; // right of the last column: the content ends before the view does
    }

    ::hwshqtb::gui::extent_t table_view_t::size_hint() const {
        const style_t& style = default_style();
        const ::hwshqtb::gui::extent_t hint{
            std::min(content_width(), style.pixel_metric(pixel_metric_enum::TABLE_MAX_WIDTH)),
            style.pixel_metric(pixel_metric_enum::LIST_VIEW_HEIGHT),
        };
        return hint;
    }

    ::hwshqtb::gui::extent_t table_view_t::minimum_size_hint() const {
        const style_t& style = default_style();
        const int bars = 2 * style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
        const ::hwshqtb::gui::extent_t hint{
            column_width(0) + bars,
            header_height() + row_height() + bars,
        };
        return hint;
    }

    bool table_view_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        if (!is_enabled()) {
            return abstract_item_view_t::interact(in);
        }
        const bool key_step = in.key != 0 || !in.text_input.empty();
        if (key_step || !in.mouse_pressed) {
            return abstract_item_view_t::interact(in);
        }
        if (in.hovered && in.mouse.y() < destination_rect().y()) {
            // The header strip is not row area: a press there neither moves the current cell nor
            // clears the selection, so it is consumed and nothing else happens.
            return true;
        }
        const int column = column_at(in.mouse);
        const bool consumed = abstract_item_view_t::interact(in);
        // The base's press decides whether a row became current; the column of the press belongs to
        // that same cell. A press that selected no row leaves no column behind either.
        set_current_column(current_row() >= 0 ? column : -1);
        return consumed;
    }

    int table_view_t::column_count() const {
        const item_model_t* const source = model();
        if (source == nullptr) {
            return 0;
        }
        return std::max(0, source->column_count());
    }

    int table_view_t::first_visible_column() const {
        const int count = column_count();
        if (count <= 0 || destination_rect().width() <= 0) {
            return 0;
        }
        const int offset = scroll_offset().x();
        int left = 0;
        for (int column = 0; column < count; ++column) {
            const int next = left + column_width(column);
            if (next > offset) {
                return column; // the first column whose band reaches past the left edge
            }
            left = next;
        }
        return count; // only zero-width columns were left: nothing to ask the model for
    }

    int table_view_t::last_visible_column() const {
        const int count = column_count();
        const int width = destination_rect().width();
        if (count <= 0 || width <= 0) {
            return -1;
        }
        const int offset = scroll_offset().x();
        const int right = offset + width;
        int left = 0;
        int last = -1;
        for (int column = 0; column < count; ++column) {
            const int next = left + column_width(column);
            if (next > offset && left < right) {
                last = column;
            }
            left = next;
        }
        return last;
    }

    int table_view_t::content_width() const {
        int width = 0;
        const int count = column_count();
        for (int column = 0; column < count; ++column) {
            width += column_width(column);
        }
        return width;
    }

    ::hwshqtb::gui::rect_t table_view_t::destination_for_body(
        ::hwshqtb::gui::rect_t body) const {
        const int header = header_height();
        return ::hwshqtb::gui::rect_t{
            body.x(),
            body.y() + header,
            body.width(),
            std::max(0, body.height() - header),
        };
    }

    void table_view_t::update_layout() {
        abstract_item_view_t::update_layout();
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        // The header is placed ABOVE the destination, never over it: its height is what the
        // destination already reserved. Hiding it therefore changes no geometry at all.
        set_child_geometry_silently(*_header_item,
                                    ::hwshqtb::gui::rect_t{ destination.x(), rect().y(),
                                                            destination.width(), header_height() });
    }

    void table_view_t::body_scrolled() {
        abstract_item_view_t::body_scrolled();
        // The titles follow the horizontal offset, and the strip is not part of the shifted content:
        // every scroll repaints it in place.
        damage_header();
    }

    void table_view_t::content_change_event(content_change_event_t*) {
        damage_header(); // the titles come from the model that just changed
    }

    void table_view_t::set_current_column(int column) {
        const int count = column_count();
        const int target = (column < 0 || count <= 0) ? -1 : std::min(column, count - 1);
        if (target == _current_column) {
            return;
        }
        _current_column = target;
        update_current_row(); // the current-cell outline left one cell and entered another
    }

    void table_view_t::update_current_row() {
        const int row = current_row();
        if (row < 0) {
            return;
        }
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        const ::hwshqtb::gui::rect_t band{
            destination.x(),
            destination.y() + row * row_height() - scroll_offset().y(),
            destination.width(),
            row_height(),
        };
        const ::hwshqtb::gui::rect_t visible = band.intersected(destination);
        if (!visible.isEmpty()) {
            update(visible);
        }
    }

    void table_view_t::damage_header() {
        _header_item->update();
    }

    void table_view_t::paint_header(::hwshqtb::gui::painter_t& painter) const {
        const ::hwshqtb::gui::rect_t strip = _header_item->rect();
        if (strip.width() <= 0 || strip.height() <= 0) {
            return;
        }
        control_state_t state;
        state.enabled = is_enabled();
        // The strip first: it also covers the part of the band right of the last column, where no
        // column title lands.
        default_style().draw_control(control_element_enum::TABLE_HEADER, painter, strip, state);
        const item_model_t* const source = model();
        if (source == nullptr) {
            return;
        }
        const int offset = scroll_offset().x();
        const int first = first_visible_column();
        const int last = last_visible_column();
        for (int column = first; column <= last; ++column) {
            std::string owned;
            const std::string_view text =
                text_of(source->header_data(column, item_role_enum::DISPLAY), owned);
            if (text.empty()) {
                continue; // the strip already painted this cell's background
            }
            const ::hwshqtb::gui::rect_t cell{
                strip.x() + column_left(column) - offset,
                strip.y(),
                column_width(column),
                strip.height(),
            };
            const ::hwshqtb::gui::rect_t visible = cell.intersected(strip);
            if (visible.isEmpty()) {
                continue;
            }
            state.text = text;
            painter.save();
            painter.set_clip(visible); // a title is cut at its column's edges, never bleeding over
            default_style().draw_control(control_element_enum::TABLE_HEADER, painter, cell, state);
            painter.restore();
        }
    }

    void table_view_t::paint_row(::hwshqtb::gui::painter_t& painter, int row,
                                 ::hwshqtb::gui::rect_t rect) const {
        abstract_item_view_t::paint_row(painter, row, rect);
        const item_model_t* const source = model();
        if (source == nullptr) {
            return;
        }
        const ::hwshqtb::gui::rect_t source_rect = this->source_rect();
        const int first = first_visible_column();
        const int last = last_visible_column();
        const int grid = std::max(0, default_style().pixel_metric(pixel_metric_enum::TABLE_GRID_WIDTH));
        const bool focused = has_focus();
        control_state_t state;
        state.enabled = is_enabled();
        state.checked = is_row_selected(row);
        for (int column = first; column <= last; ++column) {
            const int width = column_width(column);
            if (width <= 0) {
                continue; // a hidden column has neither a cell nor a grid line
            }
            // rect() is in content coordinates: its x is 0, so the column's content coordinate is
            // all that has to be added.
            const ::hwshqtb::gui::rect_t cell{ rect.x() + column_left(column), rect.y(), width,
                                               rect.height() };
            const ::hwshqtb::gui::rect_t visible = cell.intersected(source_rect);
            if (visible.isEmpty()) {
                continue;
            }
            if (grid > 0 && column > 0) {
                // The boundary line sits in the FIRST pixel column of the cell on its right, so the
                // leftmost column has no line and the current row's outline is never overpainted.
                const ::hwshqtb::gui::rect_t line{ cell.x(), cell.y(), grid, cell.height() };
                default_style().draw_control(control_element_enum::TABLE_GRID, painter, line, state);
            }
            std::string owned;
            const std::string_view text =
                text_of(source->data(row, column, item_role_enum::DISPLAY), owned);
            state.text = text;
            state.focused = focused && row == current_row() && column == current_column();
            if (text.empty() && !state.focused) {
                continue; // nothing to draw, but the model was still asked
            }
            painter.save();
            painter.set_clip(visible);
            default_style().draw_control(control_element_enum::TABLE_CELL, painter, cell, state);
            painter.restore();
        }
    }

    bool table_view_t::handle_extra_key(const ::hwshqtb::gui::interaction_t& in) {
        if (in.key == ::hwshqtb::gui::interaction_t::return_key) {
            if (current_row() >= 0 && current_column() >= 0) {
                cell_activated(current_row(), current_column());
            }
            // A table's Return is its cell activation: the inherited row activation stays silent,
            // because the row alone is not what the user picked.
            return true;
        }
        if (in.key != ::hwshqtb::gui::interaction_t::left_key
            && in.key != ::hwshqtb::gui::interaction_t::right_key) {
            return false;
        }
        const int count = column_count();
        const item_model_t* const source = model();
        if (count <= 0 || source == nullptr || source->row_count() <= 0) {
            return false; // no cell to move through
        }
        int column = current_column();
        if (column < 0) {
            column = 0; // no current cell yet: either arrow establishes the first column
        }
        else if (in.key == ::hwshqtb::gui::interaction_t::right_key) {
            column = std::min(column + 1, count - 1);
        }
        else {
            column = std::max(column - 1, 0);
        }
        set_current_cell(current_row() < 0 ? 0 : current_row(), column);
        return true;
    }

    META_OBJECT(table_view_t);
    META_REGISTER_WIDGET(table_view_t);
}
