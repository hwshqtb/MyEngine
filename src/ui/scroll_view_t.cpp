/// @file scroll_view_t.cpp
/// @brief scroll_view_t: the two bars, the offsets and ranges, the source-rectangle scroll and the
///        pixel-shift fast path (see scroll_view_t.h).

#include "hwshqtb/ui/scroll_view_t.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <vector>

#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/scroll_bar_t.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    namespace {
        /// @brief The body extent a scroll view keeps even at its smallest: one pixel per axis.
        constexpr int minimal_viewport = 1;

        /// @brief The style's bar thickness, never negative.
        int bar_thickness() {
            return std::max(0,
                            default_style().pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS));
        }
    }

    scroll_view_t::scroll_view_t():
        scroll_view_t(nullptr) { }

    scroll_view_t::scroll_view_t(widget_t* parent):
        viewport_t(parent) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        // The frame and the content fill the whole rectangle before the bars are painted over it, so
        // a sibling under this view is completely hidden.
        set_opaque(true);
        _vertical_bar = new scroll_bar_t{ this };
        _vertical_bar->set_orientation(orientation_enum::VERTICAL);
        _vertical_bar->connect("value_changed(int) const", [this](int value) {
            if (_updating) {
                return; // the view itself is writing the bar
            }
            if (value != _vertical_offset) {
                set_vertical_offset(value);
            }
        });
        // The horizontal bar is created detached and hidden before it is attached: set_visible()
        // records a repaint, and a parentless item has no view to record it in.
        _horizontal_bar = new scroll_bar_t;
        _horizontal_bar->set_orientation(orientation_enum::HORIZONTAL);
        _horizontal_bar->set_visible(false);
        _horizontal_bar->set_parent(this);
        _horizontal_bar->connect("value_changed(int) const", [this](int value) {
            if (_updating) {
                return; // the view itself is writing the bar
            }
            if (value != _horizontal_offset) {
                set_horizontal_offset(value);
            }
        });
        update_layout();
    }

    void scroll_view_t::set_content(item_t* content) {
        if (content == viewport_t::content()) {
            return;
        }
        if (content != nullptr) {
            // Refuse the view itself, its bars and any ancestor of the view: the content must be a
            // descendant the view can own without creating a cycle.
            if (content == this || content == _vertical_bar || content == _horizontal_bar) {
                return;
            }
            for (::hwshqtb::metaobject::object_t* ancestor = parent(); ancestor != nullptr;
                 ancestor = ancestor->parent()) {
                if (ancestor == content) {
                    return;
                }
            }
            content->set_parent(this);
        }
        item_t* const previous = viewport_t::content();
        viewport_t::set_content(content);
        delete previous; // the view owns its content (Qt's QScrollArea::setWidget)
        update_layout();
    }

    ::hwshqtb::gui::extent_t scroll_view_t::content_extent() const {
        const item_t* const shown = viewport_t::content();
        if (shown == nullptr) {
            return ::hwshqtb::gui::extent_t{};
        }
        return shown->effective_size_hint();
    }

    void scroll_view_t::set_scroll_offset(::hwshqtb::gui::point_t offset) {
        update_layout(); // make the ranges current before the offsets clamp
        apply_offsets(offset.x(), offset.y());
    }

    ::hwshqtb::gui::point_t scroll_view_t::scroll_offset() const noexcept {
        return ::hwshqtb::gui::point_t{ _horizontal_offset, _vertical_offset };
    }

    int scroll_view_t::vertical_offset() const noexcept {
        return _vertical_offset;
    }

    int scroll_view_t::horizontal_offset() const noexcept {
        return _horizontal_offset;
    }

    int scroll_view_t::vertical_range() const noexcept {
        return _vertical_range;
    }

    int scroll_view_t::horizontal_range() const noexcept {
        return _horizontal_range;
    }

    void scroll_view_t::set_scroll_bar_policy(scroll_bar_policy_enum policy) {
        if (_policy == policy) {
            return;
        }
        _policy = policy;
        update_layout();
    }

    scroll_bar_policy_enum scroll_view_t::scroll_bar_policy() const {
        return _policy;
    }

    scroll_bar_t* scroll_view_t::vertical_scroll_bar() const {
        return _vertical_bar;
    }

    scroll_bar_t* scroll_view_t::horizontal_scroll_bar() const {
        return _horizontal_bar;
    }

    ::hwshqtb::gui::rect_t scroll_view_t::body_rect() const {
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int thickness = bar_thickness();
        const int vertical_thickness = std::min(thickness, std::max(0, bounds.width()));
        const int horizontal_thickness = std::min(thickness, std::max(0, bounds.height()));
        return ::hwshqtb::gui::rect_t{
            bounds.x(),
            bounds.y(),
            std::max(0, bounds.width()
                           - (_vertical_bar->is_visible() ? vertical_thickness : 0)),
            std::max(0, bounds.height()
                           - (_horizontal_bar->is_visible() ? horizontal_thickness : 0)),
        };
    }

    ::hwshqtb::gui::rect_t scroll_view_t::destination_for_body(
        ::hwshqtb::gui::rect_t body) const {
        return body;
    }

    bool scroll_view_t::horizontal_bar_needed(int viewport_width) const {
        return content_extent().width() > viewport_width;
    }

    void scroll_view_t::ensure_visible(item_t* item) {
        item_t* const shown = viewport_t::content();
        if (shown == nullptr || item == nullptr) {
            return;
        }
        // The item's top-left in the CONTENT's coordinates: accumulate the geometry origins on the
        // way up to (and excluding) the content.
        ::hwshqtb::gui::point_t position{};
        const item_t* current = item;
        while (current != shown) {
            ::hwshqtb::metaobject::object_t* const parent = current->parent();
            if (parent == nullptr || !parent->is_item()) {
                return; // the item does not live inside the content
            }
            position += current->geometry().top_left();
            current = static_cast<const item_t*>(parent);
        }
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        ::hwshqtb::gui::point_t offset = scroll_offset();
        if (position.x() < offset.x()) {
            offset.set_x(position.x());
        }
        else if (position.x() + item->rect().width() > offset.x() + destination.width()) {
            offset.set_x(position.x() + item->rect().width() - destination.width());
        }
        if (position.y() < offset.y()) {
            offset.set_y(position.y());
        }
        else if (position.y() + item->rect().height() > offset.y() + destination.height()) {
            offset.set_y(position.y() + item->rect().height() - destination.height());
        }
        set_scroll_offset(offset);
    }

    ::hwshqtb::gui::extent_t scroll_view_t::size_hint() const {
        const item_t* const shown = viewport_t::content();
        if (shown == nullptr) {
            return ::hwshqtb::gui::extent_t{};
        }
        // The body's minimal usable size, not the content's natural size: the view has to show the
        // content's floor, and content that overflows only enlarges the scroll range.
        const ::hwshqtb::gui::extent_t content_floor = shown->effective_minimum_size();
        const ::hwshqtb::gui::extent_t body{
            std::max(minimal_viewport, content_floor.width()),
            std::max(minimal_viewport, content_floor.height()),
        };
        if (_policy == scroll_bar_policy_enum::ALWAYS_OFF) {
            return body;
        }
        const int thickness = bar_thickness();
        return ::hwshqtb::gui::extent_t{ body.width() + thickness, body.height() + thickness };
    }

    ::hwshqtb::gui::extent_t scroll_view_t::minimum_size_hint() const {
        // The two bars are child items of the view, so the smallest layout still holds both of them
        // plus a body one pixel wide and tall. The content takes no part: it can always scroll.
        if (_policy == scroll_bar_policy_enum::ALWAYS_OFF) {
            return ::hwshqtb::gui::extent_t{ minimal_viewport, minimal_viewport };
        }
        const int thickness = bar_thickness();
        return ::hwshqtb::gui::extent_t{ minimal_viewport + thickness,
                                         minimal_viewport + thickness };
    }

    bool scroll_view_t::any_bar_visible() const {
        return _vertical_bar->is_visible() || _horizontal_bar->is_visible();
    }

    bool scroll_view_t::content_has_focus() const {
        const item_t* const shown = viewport_t::content();
        if (shown == nullptr) {
            return false;
        }
        std::vector<const item_t*> pending{ shown };
        while (!pending.empty()) {
            const item_t* const item = pending.back();
            pending.pop_back();
            if (item->has_focus()) {
                return true;
            }
            for (std::size_t index = 0; index < item->item_count(); ++index) {
                if (const item_t* const child = item->item_at_index(index)) {
                    pending.push_back(child);
                }
            }
        }
        return false;
    }

    void scroll_view_t::set_child_geometry_silently(widget_t& widget,
                                                    ::hwshqtb::gui::rect_t rect) {
        if (widget.geometry() == rect) {
            return;
        }
        widget.set_geometry_silently(rect);
        // A silent write skips set_geometry()'s relayout too, and a content widget's own layout
        // (the rows of a panel, the labels of a form) must still be applied to the new extent.
        widget.relayout();
    }

    void scroll_view_t::place_content() {
        item_t* const shown = viewport_t::content();
        if (shown == nullptr) {
            return;
        }
        const ::hwshqtb::gui::extent_t size = shown->effective_size_hint();
        const ::hwshqtb::gui::rect_t geometry{ 0, 0, size.width(), size.height() };
        if (shown->geometry() == geometry) {
            return;
        }
        if (shown->is_widget()) {
            // The content's place is silent: the paint path and the damage records own the repaint,
            // and a geometry write per scroll step would repaint the whole content on every step.
            set_child_geometry_silently(*static_cast<widget_t*>(shown), geometry);
        }
        else {
            shown->set_geometry(geometry);
        }
    }

    void scroll_view_t::update_layout() {
        if (_updating) {
            return;
        }
        _updating = true;
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int thickness = bar_thickness();
        const int vertical_thickness = std::min(thickness, std::max(0, bounds.width()));
        const int horizontal_thickness = std::min(thickness, std::max(0, bounds.height()));
        const ::hwshqtb::gui::extent_t content = content_extent();

        // AS_NEEDED with two bars is circular (a bar eats body space and can make the other
        // necessary). Start with both shown and iterate: hiding a bar only enlarges the destination
        // and therefore can only SHRINK a range, so the loop reaches its fixed point in at most two
        // changes; the third pass is a safety stop.
        bool show_vertical = false;
        bool show_horizontal = false;
        if (_policy != scroll_bar_policy_enum::ALWAYS_OFF) {
            const bool always = _policy == scroll_bar_policy_enum::ALWAYS_ON;
            show_vertical = true;
            show_horizontal = true;
            for (int pass = 0; pass < 3; ++pass) {
                const ::hwshqtb::gui::rect_t body{
                    bounds.x(),
                    bounds.y(),
                    std::max(0, bounds.width() - (show_vertical ? vertical_thickness : 0)),
                    std::max(0, bounds.height() - (show_horizontal ? horizontal_thickness : 0)),
                };
                const ::hwshqtb::gui::rect_t destination = destination_for_body(body);
                const bool next_vertical = always || content.height() > destination.height();
                const bool next_horizontal =
                    always || horizontal_bar_needed(destination.width());
                if (next_vertical == show_vertical && next_horizontal == show_horizontal) {
                    break;
                }
                show_vertical = next_vertical;
                show_horizontal = next_horizontal;
            }
        }

        const ::hwshqtb::gui::rect_t body{
            bounds.x(),
            bounds.y(),
            std::max(0, bounds.width() - (show_vertical ? vertical_thickness : 0)),
            std::max(0, bounds.height() - (show_horizontal ? horizontal_thickness : 0)),
        };
        const ::hwshqtb::gui::rect_t destination = destination_for_body(body);
        set_destination_rect(destination);
        // A hidden bar has no range: its axis cannot scroll (ALWAYS_OFF, or a content that fits).
        _vertical_range = show_vertical ? std::max(0, content.height() - destination.height()) : 0;
        _horizontal_range =
            show_horizontal ? std::max(0, content.width() - destination.width()) : 0;
        _vertical_offset = std::clamp(_vertical_offset, 0, _vertical_range);
        _horizontal_offset = std::clamp(_horizontal_offset, 0, _horizontal_range);
        set_source_rect(::hwshqtb::gui::rect_t{ _horizontal_offset, _vertical_offset,
                                                destination.width(), destination.height() });

        const ::hwshqtb::gui::rect_t vertical_geometry{
            bounds.right() - vertical_thickness,
            bounds.y(),
            vertical_thickness,
            body.height(),
        };
        const ::hwshqtb::gui::rect_t horizontal_geometry{
            bounds.x(),
            bounds.bottom() - horizontal_thickness,
            body.width(),
            horizontal_thickness,
        };
        if (_vertical_bar->is_visible() != show_vertical) {
            _vertical_bar->set_visible(show_vertical);
        }
        if (_vertical_bar->geometry() != vertical_geometry) {
            _vertical_bar->set_geometry(vertical_geometry);
        }
        const int old_minimum = _vertical_bar->minimum();
        const int old_maximum = _vertical_bar->maximum();
        const int old_page_step = _vertical_bar->page_step();
        _vertical_bar->set_page_step(std::max(1, destination.height()));
        _vertical_bar->set_range(0, show_vertical ? _vertical_range : 0);
        if (_vertical_bar->minimum() != old_minimum || _vertical_bar->maximum() != old_maximum
            || _vertical_bar->page_step() != old_page_step) {
            _vertical_bar->update(); // the handle's size/position follows range and page step
        }
        if (_vertical_bar->value() != _vertical_offset) {
            _vertical_bar->set_value(_vertical_offset);
        }

        if (_horizontal_bar->is_visible() != show_horizontal) {
            _horizontal_bar->set_visible(show_horizontal);
        }
        if (_horizontal_bar->geometry() != horizontal_geometry) {
            _horizontal_bar->set_geometry(horizontal_geometry);
        }
        const int old_horizontal_minimum = _horizontal_bar->minimum();
        const int old_horizontal_maximum = _horizontal_bar->maximum();
        const int old_horizontal_page_step = _horizontal_bar->page_step();
        _horizontal_bar->set_page_step(std::max(1, destination.width()));
        _horizontal_bar->set_range(0, show_horizontal ? _horizontal_range : 0);
        if (_horizontal_bar->minimum() != old_horizontal_minimum
            || _horizontal_bar->maximum() != old_horizontal_maximum
            || _horizontal_bar->page_step() != old_horizontal_page_step) {
            _horizontal_bar->update();
        }
        if (_horizontal_bar->value() != _horizontal_offset) {
            _horizontal_bar->set_value(_horizontal_offset);
        }
        place_content();
        _updating = false;
    }

    void scroll_view_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        update_layout(); // the bars and the source rectangle must be current before the paint
        viewport_t::paint_event(event);
    }

    ::hwshqtb::gui::rect_t scroll_view_t::copiable_rect() const {
        const ::hwshqtb::gui::rect_t bounds = rect();
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        const int frame = std::max(0, default_style().pixel_metric(pixel_metric_enum::BORDER_WIDTH));
        // The frame is drawn inside the widget's own edges; a destination edge that reaches an edge
        // of the widget therefore carries that edge's frame line. A hidden bar on the right/bottom
        // puts the corresponding line inside the destination too. Those lines are decoration: they
        // are excluded from the shift and repainted in place.
        const int frame_left = destination.x() == bounds.x() ? frame : 0;
        const int frame_top = destination.y() == bounds.y() ? frame : 0;
        const int frame_right = destination.right() == bounds.right() ? frame : 0;
        const int frame_bottom = destination.bottom() == bounds.bottom() ? frame : 0;
        return ::hwshqtb::gui::rect_t{
            destination.x() + frame_left,
            destination.y() + frame_top,
            std::max(0, destination.width() - frame_left - frame_right),
            std::max(0, destination.height() - frame_top - frame_bottom),
        };
    }

    void scroll_view_t::damage_decoration() {
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        const ::hwshqtb::gui::rect_t content = copiable_rect();
        const ::hwshqtb::gui::rect_t bands[] = {
            ::hwshqtb::gui::rect_t{ destination.x(), destination.y(), destination.width(),
                                    content.y() - destination.y() },
            ::hwshqtb::gui::rect_t{ destination.x(), content.bottom(), destination.width(),
                                    destination.bottom() - content.bottom() },
            ::hwshqtb::gui::rect_t{ destination.x(), content.y(), content.x() - destination.x(),
                                    content.height() },
            ::hwshqtb::gui::rect_t{ content.right(), content.y(),
                                    destination.right() - content.right(), content.height() },
        };
        for (const ::hwshqtb::gui::rect_t& band : bands) {
            if (!band.isEmpty()) {
                update(band);
            }
        }
    }

    void scroll_view_t::scroll_body(int dx, int dy) {
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        if (destination.isEmpty() || (dx == 0 && dy == 0)) {
            return;
        }
        const ::hwshqtb::gui::rect_t content = copiable_rect();
        // The decoration is repainted at its fixed place on every step, including the repaint
        // fallback below: the frame never moves.
        damage_decoration();
        if (content.width() <= 0 || content.height() <= 0 || std::abs(dx) >= content.width()
            || std::abs(dy) >= content.height()) {
            update(destination); // a shift of a whole destination reveals nothing already painted
            return;
        }
        // Already-painted content pixels move; the host shifts them and repaints the band the shift
        // uncovers. Without a scroll sink widget_t falls back to damaging the view.
        notify_scroll(content, dx, dy);
    }

    void scroll_view_t::body_scrolled() { }

    void scroll_view_t::sync_bar_value(scroll_bar_t& bar, int value) {
        if (_updating || bar.value() == value) {
            return;
        }
        _updating = true;
        bar.set_value(value); // the bar records its own repaint
        _updating = false;
    }

    void scroll_view_t::apply_offsets(int horizontal, int vertical) {
        const int target_horizontal = std::clamp(horizontal, 0, _horizontal_range);
        const int target_vertical = std::clamp(vertical, 0, _vertical_range);
        if (target_horizontal != _horizontal_offset || target_vertical != _vertical_offset) {
            const int dx = target_horizontal - _horizontal_offset;
            const int dy = target_vertical - _vertical_offset;
            _horizontal_offset = target_horizontal;
            _vertical_offset = target_vertical;
            const ::hwshqtb::gui::rect_t destination = destination_rect();
            set_source_rect(::hwshqtb::gui::rect_t{ _horizontal_offset, _vertical_offset,
                                                    destination.width(), destination.height() });
            scroll_body(-dx, -dy);
            body_scrolled();
        }
        sync_bar_value(*_horizontal_bar, _horizontal_offset);
        sync_bar_value(*_vertical_bar, _vertical_offset);
    }

    void scroll_view_t::set_vertical_offset(int offset) {
        apply_offsets(_horizontal_offset, offset);
    }

    void scroll_view_t::set_horizontal_offset(int offset) {
        apply_offsets(offset, _vertical_offset);
    }

    void scroll_view_t::reset_offsets() {
        _horizontal_offset = 0;
        _vertical_offset = 0;
        const ::hwshqtb::gui::rect_t destination = destination_rect();
        set_source_rect(::hwshqtb::gui::rect_t{ 0, 0, destination.width(), destination.height() });
        sync_bar_value(*_horizontal_bar, _horizontal_offset);
        sync_bar_value(*_vertical_bar, _vertical_offset);
    }

    void scroll_view_t::geometry_change_event(geometry_change_event_t* event) {
        widget_t::geometry_change_event(event); // the widget layer's re-layout and frame request
        update_layout();                        // ... then the scroll state follows the new rectangle
    }

    bool scroll_view_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        if (!is_enabled()) {
            return false;
        }
        update_layout(); // keep the ranges current before acting on them

        if (in.key != 0) {
            // The view owns the keys only while IT holds the focus; a focused control inside the
            // content is the more specific target and keeps its keys.
            if (!has_focus() || content_has_focus() || !any_bar_visible()) {
                return false;
            }
            ::hwshqtb::gui::point_t offset = scroll_offset();
            bool handled = true;
            const int vertical_single = _vertical_bar->single_step();
            const int horizontal_single = _horizontal_bar->single_step();
            if (in.key == ::hwshqtb::gui::interaction_t::left_key) {
                offset.set_x(offset.x() - horizontal_single);
            }
            else if (in.key == ::hwshqtb::gui::interaction_t::right_key) {
                offset.set_x(offset.x() + horizontal_single);
            }
            else if (in.key == ::hwshqtb::gui::interaction_t::up_key) {
                offset.set_y(offset.y() - vertical_single);
            }
            else if (in.key == ::hwshqtb::gui::interaction_t::down_key) {
                offset.set_y(offset.y() + vertical_single);
            }
            else if (in.key == ::hwshqtb::gui::interaction_t::page_up_key) {
                offset.set_y(offset.y() - _vertical_bar->page_step());
            }
            else if (in.key == ::hwshqtb::gui::interaction_t::page_down_key) {
                offset.set_y(offset.y() + _vertical_bar->page_step());
            }
            else if (in.key == ::hwshqtb::gui::interaction_t::home_key) {
                offset = ::hwshqtb::gui::point_t{};
            }
            else if (in.key == ::hwshqtb::gui::interaction_t::end_key) {
                offset = ::hwshqtb::gui::point_t{ _horizontal_bar->maximum(),
                                                  _vertical_bar->maximum() };
            }
            else {
                handled = false;
            }
            if (!handled) {
                return false;
            }
            if (in.key_pressed) {
                set_scroll_offset(offset);
            }
            return true; // the view owns the key, change or not
        }

        if (in.wheel.x() == 0 && in.wheel.y() == 0) {
            return false; // a plain mouse step belongs to the bars and the content
        }
        const int step = default_style().pixel_metric(pixel_metric_enum::SCROLL_WHEEL_STEP);
        const ::hwshqtb::gui::point_t before = scroll_offset();
        ::hwshqtb::gui::point_t offset = before;
        offset.set_x(offset.x() + in.wheel.x() * step); // a wheel to the right scrolls right
        offset.set_y(offset.y() - in.wheel.y() * step); // a wheel away from the user scrolls up
        set_scroll_offset(offset);
        return scroll_offset() != before; // consume only a step that actually moved
    }

    META_OBJECT(scroll_view_t);
    META_REGISTER_WIDGET(scroll_view_t);
}
