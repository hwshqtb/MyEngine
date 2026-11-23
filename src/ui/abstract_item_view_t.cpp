/// @file abstract_item_view_t.cpp
/// @brief abstract_item_view_t: the virtualised row grid, the selection/keyboard state machine and
///        the model notification -> damage mapping (see abstract_item_view_t.h).

#include "hwshqtb/ui/abstract_item_view_t.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    namespace {
        /// @brief The default row height: one style line plus the vertical text padding on both sides.
        int default_row_height() {
            const style_t& style = default_style();
            return std::max(1, style.pixel_metric(pixel_metric_enum::LINE_HEIGHT)
                                   + 2 * style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING));
        }

        /// @brief Append @p row to @p rows when it is not there yet, keeping the vector ascending.
        void add_row(std::vector<int>& rows, int row) {
            const auto position = std::lower_bound(rows.begin(), rows.end(), row);
            if (position == rows.end() || *position != row) {
                rows.insert(position, row);
            }
        }
    }

    /// @brief The private content item: paints the visible rows in content coordinates and reports
    ///        the row grid as its extent.
    /// @note It is disabled on purpose. Painting never checks the enabled flag, but hit testing does,
    ///       so a pointer over the rows reaches the VIEW (the row state machine) and never this item.
    class abstract_item_view_t::row_content_t final: public widget_t {
    public:
        /// @brief Construct the item for @p view (not parented yet; set_content() does that).
        /// @param view The owning view; it must outlive this item.
        explicit row_content_t(abstract_item_view_t* view):
            widget_t(nullptr),
            _view(view) {
            set_enabled(false);
        }

        /// @brief The row grid extent, asked of the view.
        ::hwshqtb::gui::extent_t size_hint() const override {
            return _view->content_extent();
        }

        /// @brief The row grid has no smaller usable size of its own.
        ::hwshqtb::gui::extent_t minimum_size_hint() const override {
            return ::hwshqtb::gui::extent_t{};
        }

    protected:
        /// @brief Paint the visible rows; the view decides which rows intersect the source.
        /// @param event The paint event; event->painter() is already translated to this item and
        ///        clipped to its rect().
        void paint_event(paint_event_t* event) override {
            _view->paint_rows(event->painter());
        }

    private:
        /// @brief The owning view (not owned).
        abstract_item_view_t* _view = nullptr;
    };

    abstract_item_view_t::abstract_item_view_t(widget_t* parent):
        scroll_view_t(parent) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        // The background plus the rows plus the bars cover every pixel of the rectangle.
        set_opaque(true);
        _row_height = default_row_height();
        _content_item = new row_content_t{ this };
        set_content(_content_item); // the view owns its one content item
        update_layout();
    }

    abstract_item_view_t::~abstract_item_view_t() {
        disconnect_model();
    }

    void abstract_item_view_t::set_model(item_model_t* model) {
        if (model == _model.data()) {
            return;
        }
        disconnect_model();
        _model = model;
        if (model != nullptr) {
            connect_model();
        }
        reset_contents();
    }

    item_model_t* abstract_item_view_t::model() const noexcept {
        return _model.data();
    }

    void abstract_item_view_t::set_row_height(int height) {
        const int next = std::max(1, height);
        if (next == _row_height) {
            return;
        }
        _row_height = next;
        update_layout(); // re-clamp the offsets for the new content extent
        update(destination_rect());
        content_change_event_t content_event; // a subclass's decoration may follow the row grid
        (void)::hwshqtb::metaobject::core_application::send_event(this, &content_event);
    }

    int abstract_item_view_t::row_height() const noexcept {
        return _row_height;
    }

    int abstract_item_view_t::current_row() const noexcept {
        return _current_row;
    }

    void abstract_item_view_t::set_current_row(int row) {
        const int count = visible_row_count();
        const int target = (row < 0 || count <= 0) ? -1 : std::min(row, count - 1);
        set_current_row_value(target);
        _anchor_row = target;
        if (_selection_mode == selection_mode_enum::SINGLE) {
            set_selection_rows(target >= 0 ? std::vector<int>{ target } : std::vector<int>{});
        }
    }

    selection_mode_enum abstract_item_view_t::selection_mode() const noexcept {
        return _selection_mode;
    }

    void abstract_item_view_t::set_selection_mode(selection_mode_enum mode) {
        if (mode == _selection_mode) {
            return;
        }
        _selection_mode = mode;
        _anchor_row = _current_row;
        if (mode == selection_mode_enum::SINGLE) {
            set_selection_rows(_current_row >= 0 ? std::vector<int>{ _current_row }
                                                 : std::vector<int>{});
        }
    }

    bool abstract_item_view_t::is_row_selected(int row) const noexcept {
        return std::binary_search(_selected_rows.begin(), _selected_rows.end(), row);
    }

    std::vector<int> abstract_item_view_t::selected_rows() const {
        return _selected_rows;
    }

    void abstract_item_view_t::scroll_to_row(int row) {
        const int count = visible_row_count();
        if (row < 0 || row >= count) {
            return;
        }
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        const int top = row * _row_height;
        const int bottom = top + _row_height;
        const int height = destination.height();
        int offset = vertical_offset();
        if (top < offset) {
            offset = top;
        }
        else if (bottom > offset + height) {
            offset = bottom - height;
        }
        set_vertical_offset(offset);
    }

    ::hwshqtb::gui::extent_t abstract_item_view_t::minimum_size_hint() const {
        const style_t& style = default_style();
        return ::hwshqtb::gui::extent_t{
            style.pixel_metric(pixel_metric_enum::LIST_VIEW_MIN_WIDTH),
            _row_height + style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS),
        };
    }

    ::hwshqtb::gui::extent_t abstract_item_view_t::content_extent() const {
        return ::hwshqtb::gui::extent_t{ content_width(), visible_row_count() * _row_height };
    }

    int abstract_item_view_t::content_width() const {
        // A row view has no content wider than the space it is given: the row band IS the body, so
        // the horizontal range is always empty.
        return body_rect().width();
    }

    bool abstract_item_view_t::horizontal_bar_needed(int viewport_width) const {
        (void)viewport_width;
        // The VIEW's width is the one extent no bar can change: comparing the content against the
        // destination would be circular for a content whose width IS the destination width (the
        // default of a row view), because the bar would shrink the destination below the boundary
        // that justified it.
        return content_extent().width() > rect().width();
    }

    bool abstract_item_view_t::handle_extra_key(const ::hwshqtb::gui::interaction_t& in) {
        (void)in;
        return false; // the base's keys are the whole keyboard contract of a plain row view
    }

    void abstract_item_view_t::content_change_event(content_change_event_t*) { }

    bool abstract_item_view_t::event(::hwshqtb::metaobject::event_t* event) {
        if (event != nullptr) {
            switch (event->type()) {
                case ::hwshqtb::metaobject::event_type_enum::UI_DATA_CHANGE:
                    data_change_event(static_cast<data_change_event_t*>(event));
                    event->accept();
                    return true;
                case ::hwshqtb::metaobject::event_type_enum::UI_ROWS_CHANGE:
                    rows_change_event(static_cast<rows_change_event_t*>(event));
                    event->accept();
                    return true;
                default:
                    break;
            }
        }
        return item_t::event(event);
    }

    void abstract_item_view_t::paint_rows(::hwshqtb::gui::painter_t& painter) {
        const int count = visible_row_count();
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        if (count <= 0 || destination.width() <= 0 || destination.height() <= 0) {
            return;
        }
        const int offset = vertical_offset();
        // Only the rows that intersect the destination are asked for: the paint cost is a function
        // of the destination, not of the model.
        const int first = std::min(count - 1, offset / _row_height);
        const int last = std::min(count - 1, (offset + destination.height() - 1) / _row_height);
        const ::hwshqtb::gui::rect_t source = source_rect();
        for (int row = first; row <= last; ++row) {
            const ::hwshqtb::gui::rect_t area{ 0, row * _row_height, content_width(), _row_height };
            if (area.intersected(source).isEmpty()) {
                continue;
            }
            // Paint the WHOLE row and let the destination's clip cut the part outside it: the row's
            // own pixels (its content's position, the current outline) must not depend on how much
            // of the row happens to be visible, otherwise the same row paints differently at
            // different scroll offsets and a shifted frame no longer matches a full repaint.
            paint_row(painter, row, area);
        }
    }

    void abstract_item_view_t::body_scrolled() {
        // The pointer did not move but the content under it did: the hovered row is the row under the
        // remembered position at the NEW offset, so the highlight follows the pointer instead of
        // staying on the content row the last pointer step happened to see.
        if (_has_hover_position) {
            set_hover_row(row_at(_hover_position));
        }
    }

    bool abstract_item_view_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        if (!is_enabled()) {
            return false;
        }
        update_layout(); // keep the bars and the offsets current before acting on them
        if (in.key != 0 || !in.text_input.empty()) {
            // A key step carries no pointer position of its own: the remembered one stays the truth a
            // scroll has to re-derive the hovered row from.
            return handle_key(in);
        }
        // The pointer position is what the hovered row is derived from, so it is remembered for every
        // pointer step: a scroll moves the content under a pointer that did not move, and the row
        // under it then belongs to the new offset, not to the one the last pointer step saw.
        _has_hover_position = in.hovered;
        if (in.hovered) {
            _hover_position = in.mouse;
        }
        if (in.mouse_pressed) {
            return handle_press(in);
        }
        if (in.mouse_released) {
            return handle_release(in);
        }
        if (in.wheel.x() != 0 || in.wheel.y() != 0) {
            return handle_wheel(in);
        }
        return handle_move(in);
    }

    void abstract_item_view_t::hover_event(hover_event_t* event) {
        // Hover is per ROW, and rows are not items: set_hover_row() records exactly the old and new
        // row bands, so marking the whole view here would repaint the content on every row change.
        // An EXTERNAL clear (the pointer left the window, the router dropped the state) has no
        // interaction step to record the row, so this handler drops the remembered row itself.
        if (event->hovered() || _hovered_row < 0) {
            return;
        }
        const int row = _hovered_row;
        _hovered_row = -1;
        damage_row(row);
    }

    int abstract_item_view_t::visible_row_count() const {
        return model_row_count();
    }

    int abstract_item_view_t::model_row_count() const {
        const item_model_t* const model = _model.data();
        if (model == nullptr) {
            return 0;
        }
        return std::max(0, model->row_count());
    }

    int abstract_item_view_t::row_at(::hwshqtb::gui::point_t local) const {
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        if (local.x() < destination.x() || local.x() >= destination.right()
            || local.y() < destination.y() || local.y() >= destination.bottom()) {
            return -1; // outside the destination: a bar or a header owns that band
        }
        const int content_y = local.y() - destination.y() + vertical_offset();
        if (content_y < 0) {
            return -1;
        }
        const int row = content_y / _row_height;
        return row < visible_row_count() ? row : -1;
    }

    ::hwshqtb::gui::rect_t abstract_item_view_t::row_rect(int row) const {
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        return ::hwshqtb::gui::rect_t{
            destination.x() - horizontal_offset(),
            destination.y() + row * _row_height - vertical_offset(),
            content_width(),
            _row_height,
        };
    }

    void abstract_item_view_t::connect_model() {
        item_model_t* const model = _model.data();
        if (model == nullptr) {
            return;
        }
        _data_connection = model->connect("data_changed(int,int) const", [this](int first, int last) {
            data_change_event_t event{ first, last };
            (void)::hwshqtb::metaobject::core_application::send_event(this, &event);
        });
        _inserted_connection = model->connect("rows_inserted(int,int) const", [this]() {
            rows_change_event_t event;
            (void)::hwshqtb::metaobject::core_application::send_event(this, &event);
        });
        _removed_connection = model->connect("rows_removed(int,int) const", [this]() {
            rows_change_event_t event;
            (void)::hwshqtb::metaobject::core_application::send_event(this, &event);
        });
        _reset_connection =
            model->connect("model_reset() const", [this]() { handle_model_reset(); });
        _destroyed_connection = model->connect(
            "destroyed(hwshqtb::metaobject::object_t*) const",
            [this]() { handle_model_destroyed(); });
    }

    void abstract_item_view_t::disconnect_model() {
        (void)_data_connection.disconnect();
        (void)_inserted_connection.disconnect();
        (void)_removed_connection.disconnect();
        (void)_reset_connection.disconnect();
        (void)_destroyed_connection.disconnect();
        _data_connection = {};
        _inserted_connection = {};
        _removed_connection = {};
        _reset_connection = {};
        _destroyed_connection = {};
        _model.clear();
    }

    void abstract_item_view_t::reset_contents() {
        const int old_current = _current_row;
        const bool had_selection = !_selected_rows.empty();
        _current_row = -1;
        _anchor_row = -1;
        _selected_rows.clear();
        _hovered_row = -1;
        reset_offsets();
        update_layout();
        update(destination_rect());
        content_change_event_t content_event; // a subclass's decoration may come from the model
        (void)::hwshqtb::metaobject::core_application::send_event(this, &content_event);
        if (old_current != -1) {
            current_changed(-1);
        }
        if (had_selection) {
            selection_changed();
        }
    }

    void abstract_item_view_t::data_change_event(data_change_event_t* event) {
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
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        for (int row = first; row <= last; ++row) {
            const ::hwshqtb::gui::rect_t band = row_rect(row).intersected(destination);
            if (!band.isEmpty()) {
                update(band);
            }
        }
    }

    void abstract_item_view_t::rows_change_event(rows_change_event_t*) {
        const int old_current = _current_row;
        update_layout(); // clamps the offsets and refreshes the bars' ranges
        const int count = visible_row_count();
        if (_current_row >= count) {
            _current_row = count > 0 ? count - 1 : -1;
        }
        if (_anchor_row >= count) {
            _anchor_row = count > 0 ? count - 1 : -1;
        }
        if (_hovered_row >= count) {
            _hovered_row = -1;
        }
        if (_selection_mode == selection_mode_enum::SINGLE) {
            // The single-selection invariant survives a structural change: the current row IS the
            // selected row. MULTI only prunes the rows that no longer exist.
            set_selection_rows(_current_row >= 0 ? std::vector<int>{ _current_row }
                                                 : std::vector<int>{});
        }
        else {
            set_selection_rows(_selected_rows);
        }
        update(destination_rect());
        content_change_event_t content_event; // the vertical bar can appear or disappear and move a
                                             // subclass's band
        (void)::hwshqtb::metaobject::core_application::send_event(this, &content_event);
        if (old_current != _current_row) {
            current_changed(_current_row);
        }
    }

    void abstract_item_view_t::remap_rows(const std::vector<int>& row_map) {
        const auto remap = [&row_map](int row) {
            if (row < 0 || static_cast<std::size_t>(row) >= row_map.size()) {
                return -1; // not covered by the map: the row is gone
            }
            return row_map[static_cast<std::size_t>(row)];
        };
        const int old_current = _current_row;
        _current_row = remap(_current_row);
        _anchor_row = remap(_anchor_row);
        _hovered_row = remap(_hovered_row);
        std::vector<int> rows;
        rows.reserve(_selected_rows.size());
        for (const int row : _selected_rows) {
            const int next = remap(row);
            if (next >= 0) {
                rows.push_back(next);
            }
        }
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        if (rows != _selected_rows) {
            // The caller repaints the rows; the announcement is still this function's.
            _selected_rows = std::move(rows);
            selection_changed();
        }
        if (_current_row != old_current) {
            current_changed(_current_row);
        }
    }

    void abstract_item_view_t::handle_model_reset() {
        reset_contents();
    }

    void abstract_item_view_t::handle_model_destroyed() {
        // The connections are cut by the emitter's destruction; only release their handles. The
        // guarded pointer is already null, so the view must reset the row-dependent state itself.
        _data_connection = {};
        _inserted_connection = {};
        _removed_connection = {};
        _reset_connection = {};
        _destroyed_connection = {};
        _model.clear();
        reset_contents();
    }

    void abstract_item_view_t::damage_row(int row) {
        if (row < 0 || row >= visible_row_count()) {
            return;
        }
        const ::hwshqtb::gui::rect_t band =
            row_rect(row).intersected(destination_rect());
        if (!band.isEmpty()) {
            update(band);
        }
    }

    void abstract_item_view_t::set_selection_rows(std::vector<int> rows) {
        const int count = visible_row_count();
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [count](int row) { return row < 0 || row >= count; }),
                   rows.end());
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        if (rows == _selected_rows) {
            return; // only a real change is announced
        }
        const std::vector<int> previous = _selected_rows;
        _selected_rows = std::move(rows);
        // A selected row is painted differently, so every row that entered or left the set has to
        // be repainted; damaging only the new ones would leave the deselected fill on screen.
        for (const int row : previous) {
            damage_row(row);
        }
        for (const int row : _selected_rows) {
            damage_row(row);
        }
        selection_changed();
    }

    std::vector<int> abstract_item_view_t::rows_between(int first, int last) const {
        std::vector<int> rows;
        const int low = std::min(first, last);
        const int high = std::max(first, last);
        for (int row = low; row <= high; ++row) {
            add_row(rows, row);
        }
        return rows;
    }

    void abstract_item_view_t::set_current_row_value(int row) {
        if (row == _current_row) {
            return;
        }
        const int old = _current_row;
        _current_row = row;
        // The outline and (in SINGLE mode) the selected fill both follow the current row: the row it
        // left has to be repainted as well as the one it entered.
        damage_row(old);
        damage_row(row);
        current_changed(row);
    }

    void abstract_item_view_t::move_current(int row, bool extend) {
        const int count = visible_row_count();
        if (count <= 0) {
            return;
        }
        const int target = std::clamp(row, 0, count - 1);
        if (extend && _selection_mode == selection_mode_enum::MULTI) {
            if (_anchor_row < 0 || _anchor_row >= count) {
                _anchor_row = _current_row >= 0 ? _current_row : target;
            }
            set_current_row_value(target);
            set_selection_rows(rows_between(_anchor_row, target));
        }
        else {
            _anchor_row = target;
            set_current_row_value(target);
            set_selection_rows({ target });
        }
        scroll_to_row(target);
    }

    void abstract_item_view_t::click_row(int row, std::uint32_t modifiers) {
        const int count = visible_row_count();
        if (row < 0 || row >= count) {
            clear_current_and_selection();
            return;
        }
        set_hover_row(row);
        const bool multi = _selection_mode == selection_mode_enum::MULTI;
        const bool shift =
            multi && ::hwshqtb::gui::has_modifier(modifiers, ::hwshqtb::gui::modifier_flag::SHIFT);
        const bool control =
            multi && ::hwshqtb::gui::has_modifier(modifiers, ::hwshqtb::gui::modifier_flag::CONTROL);
        if (shift) {
            if (_anchor_row < 0 || _anchor_row >= count) {
                _anchor_row = _current_row >= 0 ? _current_row : row;
            }
            set_current_row_value(row);
            set_selection_rows(rows_between(_anchor_row, row));
        }
        else if (control) {
            set_current_row_value(row); // the anchor does not move on a Ctrl click
            std::vector<int> rows = _selected_rows;
            const auto position = std::lower_bound(rows.begin(), rows.end(), row);
            if (position != rows.end() && *position == row) {
                rows.erase(position);
            }
            else {
                rows.insert(position, row);
            }
            set_selection_rows(std::move(rows));
        }
        else {
            _anchor_row = row;
            set_current_row_value(row);
            set_selection_rows({ row });
        }
        scroll_to_row(row);
    }

    void abstract_item_view_t::clear_current_and_selection() {
        set_current_row_value(-1);
        _anchor_row = -1;
        set_selection_rows({});
    }

    void abstract_item_view_t::set_hover_row(int row) {
        if (row == _hovered_row) {
            return;
        }
        const int old = _hovered_row;
        _hovered_row = row;
        damage_row(old);
        damage_row(row);
        set_hovered(row >= 0);
    }

    bool abstract_item_view_t::handle_key(const ::hwshqtb::gui::interaction_t& in) {
        if (!has_focus() || !in.key_pressed) {
            return false;
        }
        if (handle_extra_key(in)) {
            return true; // a key the subclass added, or one it took over before the row navigation
        }
        if (in.key == ::hwshqtb::gui::interaction_t::return_key) {
            if (_current_row >= 0) {
                row_activated(_current_row);
            }
            return true;
        }
        const int count = visible_row_count();
        if (count <= 0) {
            return false;
        }
        const int page = std::max(1, destination_rect().height() / _row_height);
        const int base = _current_row < 0 ? 0 : _current_row;
        int target = _current_row;
        if (in.key == ::hwshqtb::gui::interaction_t::up_key) {
            target = _current_row < 0 ? 0 : std::max(0, _current_row - 1);
        }
        else if (in.key == ::hwshqtb::gui::interaction_t::down_key) {
            target = _current_row < 0 ? 0 : std::min(count - 1, _current_row + 1);
        }
        else if (in.key == ::hwshqtb::gui::interaction_t::page_up_key) {
            target = std::max(0, base - page);
        }
        else if (in.key == ::hwshqtb::gui::interaction_t::page_down_key) {
            target = std::min(count - 1, base + page);
        }
        else if (in.key == ::hwshqtb::gui::interaction_t::home_key) {
            target = 0;
        }
        else if (in.key == ::hwshqtb::gui::interaction_t::end_key) {
            target = count - 1;
        }
        else {
            return false;
        }
        move_current(target, ::hwshqtb::gui::has_modifier(in.modifiers,
                                                         ::hwshqtb::gui::modifier_flag::SHIFT));
        return true;
    }

    bool abstract_item_view_t::handle_press(const ::hwshqtb::gui::interaction_t& in) {
        _press_row = row_at(in.mouse);
        click_row(_press_row, in.modifiers);
        (void)capture_pointer();
        return true;
    }

    bool abstract_item_view_t::handle_release(const ::hwshqtb::gui::interaction_t& in) {
        if (_press_row < 0) {
            return false;
        }
        const int pressed = _press_row;
        _press_row = -1;
        if (in.hovered && row_at(in.mouse) == pressed) {
            row_activated(pressed);
        }
        return true;
    }

    bool abstract_item_view_t::handle_wheel(const ::hwshqtb::gui::interaction_t& in) {
        const int step = default_style().pixel_metric(pixel_metric_enum::SCROLL_WHEEL_STEP);
        const int before = vertical_offset();
        const int before_horizontal = horizontal_offset();
        set_vertical_offset(vertical_offset() - in.wheel.y() * step); // a wheel away from the user scrolls up
        set_horizontal_offset(horizontal_offset() + in.wheel.x() * step);
        set_hover_row(row_at(in.mouse));
        return vertical_offset() != before || horizontal_offset() != before_horizontal;
    }

    bool abstract_item_view_t::handle_move(const ::hwshqtb::gui::interaction_t& in) {
        set_hover_row(in.hovered ? row_at(in.mouse) : -1);
        return false; // a plain move is not claimed, so it keeps bubbling
    }

    void abstract_item_view_t::paint_row(::hwshqtb::gui::painter_t& painter, int row,
                                         ::hwshqtb::gui::rect_t rect) const {
        const bool selected = is_row_selected(row);
        const bool current = row == _current_row;
        control_state_t state;
        state.enabled = is_enabled();
        state.hovered = row == _hovered_row && is_hovered();
        state.focused = has_focus() && current;
        state.checked = selected;
        const control_element_enum kind =
            selected ? control_element_enum::LIST_ROW_SELECTED : control_element_enum::LIST_ROW;
        default_style().draw_control(kind, painter, rect, state);
        if (current) {
            default_style().draw_control(control_element_enum::LIST_ROW_CURRENT, painter, rect,
                                         state);
        }
    }

    META_OBJECT(abstract_item_view_t);
}
