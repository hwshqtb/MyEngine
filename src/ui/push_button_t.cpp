/// @file push_button_t.cpp
/// @brief push_button_t: the click/keyboard state machine and the painting through default_style().

#include "hwshqtb/ui/push_button_t.h"

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    push_button_t::push_button_t():
        widget_t(nullptr) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        set_opaque(true); // the frame fills rect() with an opaque colour before the border and label
    }

    push_button_t::push_button_t(widget_t* parent):
        widget_t(parent) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        set_opaque(true);
    }

    void push_button_t::set_text(std::string_view text) {
        if (_text == text) {
            return;
        }
        _text.assign(text);
        update(); // the label is drawn content: the frame has to repaint it
        text_changed(_text);
    }

    std::string_view push_button_t::text() const {
        return _text;
    }

    ::hwshqtb::gui::extent_t push_button_t::size_hint() const {
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const style_t& style = default_style();
        // The RESOLVED face's extent, not "bytes x one cell": with a proportional face a character is
        // not one cell wide, and a wide character is not its UTF-8 length.
        const int text_width = metrics.text_extent(_text).width();
        return ::hwshqtb::gui::extent_t{
            text_width + 2 * style.pixel_metric(pixel_metric_enum::TEXT_PADDING),
            style.pixel_metric(pixel_metric_enum::BUTTON_HEIGHT) };
    }

    /// @brief The button refuses to be narrower than its text.
    /// @return size_hint().
    /// @note Qt's QPushButton::minimumSizeHint() is its sizeHint() for the same reason: the text is the
    ///       content, so "the layout may compress me below my text" is exactly the bug that made labels
    ///       overlap. The floor is font derived because the hint is.
    ::hwshqtb::gui::extent_t push_button_t::minimum_size_hint() const {
        return size_hint();
    }

    bool push_button_t::clicked() const {
        return _clicked;
    }

    bool push_button_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        const bool was_clicked = _clicked;
        const bool was_hovered = _hovered;
        const bool was_pressed = _pressed;
        // clicked is a PER-FRAME result: it is cleared by the next frame that drives the control.
        _clicked = false;
        _hovered = in.hovered;

        if (!is_enabled()) {
            _pressed = false;
            if (was_clicked) {
                clicked_changed(false);
            }
            if (_hovered != was_hovered || _pressed != was_pressed) {
                update(); // the appearance changed: record what has to be repainted
            }
            return false;
        }

        bool consumed = false;
        if (in.key != 0) {
            // A keyboard step: its mouse fields are only context. The button owns Space and Return
            // while it holds the focus and consumes both edges of them; every other key falls through.
            if (has_focus() && (in.key == space_key || in.key == return_key)) {
                consumed = true;
                if (in.key_pressed) {
                    // One key press is one complete press-and-release: click exactly once.
                    _clicked = true;
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
                    _clicked = true;
                }
            }
        }

        if (_hovered != was_hovered || _pressed != was_pressed) {
            update(); // hover/press feedback lives in the pixels: repaint this control
        }
        if (_clicked != was_clicked) {
            clicked_changed(_clicked);
        }
        return consumed;
    }

    void push_button_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t bounds = rect();
        control_state_t state;
        state.enabled = is_enabled();
        state.hovered = is_hovered();
        state.pressed = is_pressed(::hwshqtb::gui::mouse_button_enum::LEFT);
        state.focused = has_focus();
        state.text = _text;
        style.draw_control(control_element_enum::BUTTON_FRAME, painter, bounds, state);
        style.draw_control(control_element_enum::TEXT, painter, bounds, state);
    }

    META_OBJECT(push_button_t);
    META_REGISTER_WIDGET(push_button_t);
}
