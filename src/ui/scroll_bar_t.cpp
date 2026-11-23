/// @file scroll_bar_t.cpp
/// @brief scroll_bar_t: the proportional handle, the drag/page/keyboard state machine and the
///        painting through default_style() (see scroll_bar_t.h).

#include "hwshqtb/ui/scroll_bar_t.h"

#include <algorithm>
#include <cstdint>

#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    scroll_bar_t::scroll_bar_t():
        widget_t(nullptr) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        set_opaque(true); // the groove fills rect() before the handle is drawn inside it
    }

    scroll_bar_t::scroll_bar_t(widget_t* parent):
        widget_t(parent) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        set_opaque(true);
    }

    void scroll_bar_t::set_range(int minimum, int maximum) {
        if (maximum < minimum) {
            std::swap(minimum, maximum);
        }
        _minimum = minimum;
        _maximum = maximum;
        const int clamped = std::clamp(_value, _minimum, _maximum);
        if (clamped != _value) {
            _value = clamped;
            update(); // the handle moved: the frame has to repaint it
            value_changed(_value); // the value moved: a state notification, not a User event
        }
    }

    int scroll_bar_t::minimum() const {
        return _minimum;
    }

    int scroll_bar_t::maximum() const {
        return _maximum;
    }

    void scroll_bar_t::set_value(int value) {
        const int clamped = std::clamp(value, _minimum, _maximum);
        if (clamped == _value) {
            return;
        }
        _value = clamped;
        update(); // the handle is drawn content: the frame has to repaint it
        value_changed(_value); // state notification: the User event stays the user's own action
    }

    int scroll_bar_t::value() const {
        return _value;
    }

    void scroll_bar_t::set_page_step(int step) {
        _page_step = std::max(0, step);
    }

    int scroll_bar_t::page_step() const {
        return _page_step;
    }

    void scroll_bar_t::set_single_step(int step) {
        _single_step = std::max(0, step);
    }

    int scroll_bar_t::single_step() const {
        return _single_step;
    }

    void scroll_bar_t::set_orientation(orientation_enum orientation) {
        if (_orientation == orientation) {
            return;
        }
        _orientation = orientation;
        update(); // the groove changes axis: the frame has to repaint it
    }

    orientation_enum scroll_bar_t::orientation() const {
        return _orientation;
    }

    bool scroll_bar_t::value_changed() const {
        return _value_changed;
    }

    ::hwshqtb::gui::extent_t scroll_bar_t::size_hint() const {
        const style_t& style = default_style();
        const int length = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_LENGTH);
        const int thickness = style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS);
        return _orientation == orientation_enum::HORIZONTAL
            ? ::hwshqtb::gui::extent_t{ length, thickness }
            : ::hwshqtb::gui::extent_t{ thickness, length };
    }

    int scroll_bar_t::track_length() const {
        const ::hwshqtb::gui::rect_t bounds = rect();
        return _orientation == orientation_enum::HORIZONTAL ? bounds.width() : bounds.height();
    }

    int scroll_bar_t::handle_length() const {
        const int track = track_length();
        if (track <= 0) {
            return 0;
        }
        const style_t& style = default_style();
        const int minimum = std::min(style.pixel_metric(pixel_metric_enum::SCROLL_BAR_MIN_HANDLE), track);
        const std::int64_t range = _maximum - _minimum;
        if (range <= 0) {
            return track; // no range: the handle fills the track
        }
        const std::int64_t page = std::max(0, _page_step);
        const std::int64_t denominator = range + page;
        const int proportional = denominator > 0
            ? static_cast<int>(static_cast<std::int64_t>(track) * page / denominator)
            : 0;
        return std::clamp(proportional, minimum, track);
    }

    int scroll_bar_t::travel() const {
        return std::max(0, track_length() - handle_length());
    }

    int scroll_bar_t::handle_start() const {
        const int range = _maximum - _minimum;
        const int span = travel();
        if (range <= 0 || span <= 0) {
            return 0;
        }
        const int offset = std::clamp(_value - _minimum, 0, range);
        return static_cast<int>(static_cast<std::int64_t>(span) * offset / range);
    }

    int scroll_bar_t::axis_position(::hwshqtb::gui::point_t local) const {
        return _orientation == orientation_enum::HORIZONTAL ? local.x() : local.y();
    }

    int scroll_bar_t::value_from_handle_start(int start) const {
        const int range = _maximum - _minimum;
        const int span = travel();
        if (range <= 0 || span <= 0) {
            return _minimum;
        }
        const int clamped = std::clamp(start, 0, span);
        const std::int64_t mapped =
            (static_cast<std::int64_t>(range) * clamped + span / 2) / span; // nearest value
        return _minimum + static_cast<int>(mapped);
    }

    ::hwshqtb::gui::rect_t scroll_bar_t::handle_rect() const {
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int length = handle_length();
        if (length <= 0) {
            return ::hwshqtb::gui::rect_t{};
        }
        const int start = handle_start();
        if (_orientation == orientation_enum::HORIZONTAL) {
            return ::hwshqtb::gui::rect_t{ bounds.x() + start, bounds.y(), length, bounds.height() };
        }
        return ::hwshqtb::gui::rect_t{ bounds.x(), bounds.y() + start, bounds.width(), length };
    }

    void scroll_bar_t::set_value_from_user(int value) {
        const int clamped = std::clamp(value, _minimum, _maximum);
        if (clamped == _value) {
            return;
        }
        _value = clamped;
        _value_changed = true;
        update(); // the handle is drawn content: the frame has to repaint it
        value_changed(_value);
    }

    bool scroll_bar_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        const bool was_hovered = _hovered;
        const bool was_dragging = _dragging;
        // value_changed is a PER-FRAME result: it is cleared by the next frame that drives the control.
        _value_changed = false;
        _hovered = in.hovered;

        if (!is_enabled()) {
            _dragging = false;
            if (_hovered != was_hovered || _dragging != was_dragging) {
                update(); // the appearance changed: record what has to be repainted
            }
            return false;
        }

        bool consumed = false;
        if (in.key != 0) {
            if (has_focus()) {
                int target = _value;
                bool handled = true;
                if (in.key == ::hwshqtb::gui::interaction_t::left_key
                    || in.key == ::hwshqtb::gui::interaction_t::up_key) {
                    target -= _single_step;
                }
                else if (in.key == ::hwshqtb::gui::interaction_t::right_key
                         || in.key == ::hwshqtb::gui::interaction_t::down_key) {
                    target += _single_step;
                }
                else if (in.key == ::hwshqtb::gui::interaction_t::page_up_key) {
                    target -= _page_step;
                }
                else if (in.key == ::hwshqtb::gui::interaction_t::page_down_key) {
                    target += _page_step;
                }
                else if (in.key == ::hwshqtb::gui::interaction_t::home_key) {
                    target = _minimum;
                }
                else if (in.key == ::hwshqtb::gui::interaction_t::end_key) {
                    target = _maximum;
                }
                else {
                    handled = false;
                }
                if (handled) {
                    consumed = true; // the bar owns these keys, change or not
                    if (in.key_pressed) {
                        set_value_from_user(target);
                    }
                }
            }
        }
        else {
            const int position = axis_position(in.mouse);
            bool started_drag = false;
            if (in.mouse_pressed && in.hovered) {
                consumed = true;
                const int start = handle_start();
                const int length = handle_length();
                if (position >= start && position < start + length) {
                    // The press landed on the handle: drag it, preserving where it was grabbed. The
                    // value is NOT rewritten on this same step: the grab offset already describes the
                    // handle's position, and re-deriving the value would round it to the nearest
                    // pixel position (a one-pixel drift per grab).
                    _dragging = true;
                    _drag_offset = position - start;
                    started_drag = true;
                }
                else if (position < start) {
                    set_value_from_user(_value - _page_step); // groove page towards the minimum
                }
                else {
                    set_value_from_user(_value + _page_step); // groove page towards the maximum
                }
            }
            if (_dragging) {
                consumed = true; // keep the drag while it is held, even away from the bar
                if (in.mouse_down && !started_drag) {
                    set_value_from_user(value_from_handle_start(position - _drag_offset));
                }
            }
            if (in.mouse_released && _dragging) {
                _dragging = false;
                consumed = true;
                set_value_from_user(value_from_handle_start(position - _drag_offset));
            }
        }
        if (_hovered != was_hovered || _dragging != was_dragging) {
            update(); // hover/press feedback lives in the pixels: repaint this control
        }
        return consumed;
    }

    void scroll_bar_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        const style_t& style = default_style();
        control_state_t state;
        state.enabled = is_enabled();
        state.hovered = is_hovered();
        state.pressed = is_pressed(::hwshqtb::gui::mouse_button_enum::LEFT);
        state.focused = has_focus();
        style.draw_control(control_element_enum::SCROLL_BAR_GROOVE, painter, rect(), state);
        const ::hwshqtb::gui::rect_t handle = handle_rect();
        if (!handle.isEmpty()) {
            style.draw_control(control_element_enum::SCROLL_BAR_HANDLE, painter, handle, state);
        }
    }

    META_OBJECT(scroll_bar_t);
    META_REGISTER_WIDGET(scroll_bar_t);
}
