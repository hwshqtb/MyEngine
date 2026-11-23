/// @file slider_t.cpp
/// @brief slider_t: the position/value mapping, the drag state machine and the painting through
///        default_style() (see slider_t.h).

#include "hwshqtb/ui/slider_t.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    slider_t::slider_t():
        widget_t(nullptr) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
    }

    slider_t::slider_t(widget_t* parent):
        widget_t(parent) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
    }

    void slider_t::set_range(int minimum, int maximum) {
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

    int slider_t::minimum() const {
        return _minimum;
    }

    int slider_t::maximum() const {
        return _maximum;
    }

    void slider_t::set_value(int value) {
        const int clamped = std::clamp(value, _minimum, _maximum);
        if (clamped == _value) {
            return;
        }
        _value = clamped;
        update(); // the handle is drawn content: the frame has to repaint it
        value_changed(_value); // state notification: the User event stays the user's own action
    }

    int slider_t::value() const {
        return _value;
    }

    void slider_t::set_single_step(int step) {
        _single_step = std::max(0, step);
    }

    int slider_t::single_step() const {
        return _single_step;
    }

    void slider_t::set_page_step(int step) {
        _page_step = std::max(0, step);
    }

    int slider_t::page_step() const {
        return _page_step;
    }

    void slider_t::set_orientation(orientation_enum orientation) {
        if (_orientation == orientation) {
            return;
        }
        _orientation = orientation;
        update(); // the groove changes axis: the frame has to repaint it
    }

    orientation_enum slider_t::orientation() const {
        return _orientation;
    }

    bool slider_t::value_changed() const {
        return _value_changed;
    }

    ::hwshqtb::gui::extent_t slider_t::size_hint() const {
        const style_t& style = default_style();
        const int length = style.pixel_metric(pixel_metric_enum::SLIDER_LENGTH);
        const int cross = std::max(style.pixel_metric(pixel_metric_enum::SLIDER_HANDLE_SIZE),
                                   style.pixel_metric(pixel_metric_enum::SLIDER_GROOVE_THICKNESS));
        return _orientation == orientation_enum::HORIZONTAL ? ::hwshqtb::gui::extent_t{ length, cross }
                                                                   : ::hwshqtb::gui::extent_t{ cross, length };
    }

    ::hwshqtb::gui::rect_t slider_t::groove_rect() const {
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int thickness = style.pixel_metric(pixel_metric_enum::SLIDER_GROOVE_THICKNESS);
        if (_orientation == orientation_enum::HORIZONTAL) {
            const int height = std::min(bounds.height(), thickness);
            return ::hwshqtb::gui::rect_t{ bounds.x(), bounds.y() + (bounds.height() - height) / 2,
                                           bounds.width(), height };
        }
        const int width = std::min(bounds.width(), thickness);
        return ::hwshqtb::gui::rect_t{ bounds.x() + (bounds.width() - width) / 2, bounds.y(), width,
                                       bounds.height() };
    }

    ::hwshqtb::gui::rect_t slider_t::handle_rect() const {
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t groove = groove_rect();
        const int handle = style.pixel_metric(pixel_metric_enum::SLIDER_HANDLE_SIZE);
        const int span = _maximum - _minimum;
        const std::int64_t offset = _value - _minimum;
        if (_orientation == orientation_enum::HORIZONTAL) {
            const int track = std::max(0, groove.width() - handle);
            const int travel = span <= 0
                ? 0
                : static_cast<int>((static_cast<std::int64_t>(track) * offset + span / 2) / span);
            const int center_x = groove.x() + handle / 2 + travel;
            const int center_y = groove.y() + groove.height() / 2;
            return ::hwshqtb::gui::rect_t{ center_x - handle / 2, center_y - handle / 2, handle, handle };
        }
        const int track = std::max(0, groove.height() - handle);
        const int travel = span <= 0
            ? 0
            : static_cast<int>((static_cast<std::int64_t>(track) * offset + span / 2) / span);
        const int center_x = groove.x() + groove.width() / 2;
        const int center_y = groove.bottom() - handle / 2 - travel;
        return ::hwshqtb::gui::rect_t{ center_x - handle / 2, center_y - handle / 2, handle, handle };
    }

    int slider_t::value_from_position(::hwshqtb::gui::point_t local) const {
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t groove = groove_rect();
        const int handle = style.pixel_metric(pixel_metric_enum::SLIDER_HANDLE_SIZE);
        const int span = _maximum - _minimum;
        if (span <= 0) {
            return _minimum; // an empty range has exactly one value
        }
        int track = 0;
        int offset = 0;
        if (_orientation == orientation_enum::HORIZONTAL) {
            const int begin = groove.x() + handle / 2;
            track = groove.width() - handle;
            if (track <= 0) {
                return _minimum; // no travel space: the handle cannot move
            }
            offset = std::clamp(local.x() - begin, 0, track);
        }
        else {
            const int bottom = groove.bottom() - handle / 2;
            track = groove.height() - handle;
            if (track <= 0) {
                return _minimum;
            }
            offset = std::clamp(bottom - local.y(), 0, track); // a vertical slider grows upwards
        }
        const std::int64_t mapped =
            (static_cast<std::int64_t>(span) * offset + track / 2) / track; // nearest value
        return _minimum + static_cast<int>(mapped);
    }

    void slider_t::set_value_from_user(int value) {
        const int clamped = std::clamp(value, _minimum, _maximum);
        if (clamped == _value) {
            return;
        }
        _value = clamped;
        _value_changed = true;
        update(); // the handle is drawn content: the frame has to repaint it
        value_changed(_value);
    }

    bool slider_t::interact(const ::hwshqtb::gui::interaction_t& in) {
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
                    || in.key == ::hwshqtb::gui::interaction_t::down_key) {
                    target -= _single_step;
                }
                else if (in.key == ::hwshqtb::gui::interaction_t::right_key
                         || in.key == ::hwshqtb::gui::interaction_t::up_key) {
                    target += _single_step;
                }
                else if (in.key == ::hwshqtb::gui::interaction_t::page_down_key) {
                    target -= _page_step;
                }
                else if (in.key == ::hwshqtb::gui::interaction_t::page_up_key) {
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
                    consumed = true; // the slider owns all of these keys, change or not
                    if (in.key_pressed) {
                        set_value_from_user(target);
                    }
                }
            }
        }
        else {
            if (in.mouse_pressed && in.hovered) {
                _dragging = true;
                consumed = true;
                slider_pressed();
                set_value_from_user(value_from_position(in.mouse));
            }
            if (_dragging) {
                consumed = true; // keep the drag while it is held, even away from the groove
                if (in.mouse_down) {
                    set_value_from_user(value_from_position(in.mouse));
                }
            }
            if (in.mouse_released && _dragging) {
                _dragging = false;
                consumed = true;
                set_value_from_user(value_from_position(in.mouse));
                slider_released();
            }
        }
        if (_hovered != was_hovered || _dragging != was_dragging) {
            update(); // hover/press feedback lives in the pixels: repaint this control
        }
        return consumed;
    }

    void slider_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        const style_t& style = default_style();
        control_state_t state;
        state.enabled = is_enabled();
        state.hovered = is_hovered();
        state.pressed = is_pressed(::hwshqtb::gui::mouse_button_enum::LEFT);
        state.focused = has_focus();
        style.draw_control(control_element_enum::SLIDER_GROOVE, painter, groove_rect(), state);
        const ::hwshqtb::gui::rect_t handle = handle_rect();
        if (!handle.isEmpty()) {
            style.draw_control(control_element_enum::SLIDER_HANDLE, painter, handle, state);
        }
    }

    META_OBJECT(slider_t);
    META_REGISTER_WIDGET(slider_t);
}
