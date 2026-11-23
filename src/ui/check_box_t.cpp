/// @file check_box_t.cpp
/// @brief check_box_t: the toggle state machine and the painting through default_style().

#include "hwshqtb/ui/check_box_t.h"

#include <algorithm>

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    check_box_t::check_box_t():
        widget_t(nullptr) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
    }

    check_box_t::check_box_t(widget_t* parent):
        widget_t(parent) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
    }

    void check_box_t::set_text(std::string_view text) {
        if (_text == text) {
            return;
        }
        _text.assign(text);
        update(); // the label is drawn content: the frame has to repaint it
        text_changed(_text);
    }

    std::string_view check_box_t::text() const {
        return _text;
    }

    void check_box_t::set_checked(bool checked) {
        if (_checked == checked) {
            return;
        }
        _checked = checked;
        update(); // the indicator is drawn content: the frame has to repaint it
        toggled(checked); // state notification: the User event stays the user's own action
    }

    bool check_box_t::is_checked() const {
        return _checked;
    }

    bool check_box_t::toggled() const {
        return _toggled;
    }

    ::hwshqtb::gui::extent_t check_box_t::size_hint() const {
        const style_t& style = default_style();
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const ::hwshqtb::gui::extent_t text = metrics.text_extent(_text);
        const int width = 2 * style.pixel_metric(pixel_metric_enum::TEXT_PADDING)
            + style.pixel_metric(pixel_metric_enum::INDICATOR_SIZE)
            + style.pixel_metric(pixel_metric_enum::INDICATOR_SPACING) + text.width();
        const int height =
            std::max(style.pixel_metric(pixel_metric_enum::LINE_HEIGHT),
                     text.height() + 2 * style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING));
        return ::hwshqtb::gui::extent_t{ width, height };
    }

    /// @brief The check box refuses to be narrower than its indicator plus text (see the header).
    ::hwshqtb::gui::extent_t check_box_t::minimum_size_hint() const {
        return size_hint();
    }

    bool check_box_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        const bool was_toggled = _toggled;
        const bool was_hovered = _hovered;
        const bool was_pressed = _pressed;
        const bool was_checked = _checked;
        // toggled is a PER-FRAME result: it is cleared by the next frame that drives the control.
        _toggled = false;
        _hovered = in.hovered;

        if (!is_enabled()) {
            _pressed = false;
            if (was_toggled) {
                toggled_changed(false);
            }
            if (_hovered != was_hovered || _pressed != was_pressed) {
                update(); // the appearance changed: record what has to be repainted
            }
            return false;
        }

        bool consumed = false;
        if (in.key != 0) {
            // A keyboard step: its mouse fields are only context. Space is the one key this control
            // owns while it holds the focus, and both of its edges are consumed.
            if (has_focus() && in.key == space_key) {
                consumed = true;
                if (in.key_pressed) {
                    _checked = !_checked;
                    _toggled = true;
                    toggled(_checked);
                }
            }
        }
        else {
            if (in.mouse_pressed && in.hovered) {
                _pressed = true;
                consumed = true;
            }
            if (_pressed) {
                consumed = true; // keep the capture while held
            }
            if (in.mouse_released && _pressed) {
                const bool inside = in.hovered;
                _pressed = false;
                consumed = true;
                if (inside) {
                    _checked = !_checked;
                    _toggled = true;
                    toggled(_checked);
                }
            }
        }

        if (_hovered != was_hovered || _pressed != was_pressed || _checked != was_checked) {
            update(); // hover/press/check feedback lives in the pixels: repaint this control
        }
        if (_toggled != was_toggled) {
            toggled_changed(_toggled);
        }
        return consumed;
    }

    void check_box_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const int indicator_size = style.pixel_metric(pixel_metric_enum::INDICATOR_SIZE);
        const ::hwshqtb::gui::rect_t indicator{ bounds.x() + padding,
                                                bounds.y() + (bounds.height() - indicator_size) / 2,
                                                indicator_size, indicator_size };

        control_state_t state;
        state.enabled = is_enabled();
        state.hovered = is_hovered();
        state.pressed = is_pressed(::hwshqtb::gui::mouse_button_enum::LEFT);
        state.focused = has_focus();
        state.checked = _checked;
        style.draw_control(control_element_enum::CHECK_INDICATOR, painter, indicator, state);

        // The style centres the text in its rectangle, so the label gets a rectangle exactly as wide as
        // the text: the control places it, the style draws it.
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const int text_width = metrics.text_extent(_text).width();
        state.text = _text;
        style.draw_control(control_element_enum::TEXT, painter,
                           ::hwshqtb::gui::rect_t{ indicator.right()
                                                       + style.pixel_metric(pixel_metric_enum::INDICATOR_SPACING),
                                                   bounds.y(), text_width, bounds.height() },
                           state);
    }

    META_OBJECT(check_box_t);
    META_REGISTER_WIDGET(check_box_t);
}
