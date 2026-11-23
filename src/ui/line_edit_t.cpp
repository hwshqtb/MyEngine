/// @file line_edit_t.cpp
/// @brief line_edit_t: the UTF-8 text/caret/selection state machine and the painting through
///        default_style() (see line_edit_t.h).

#include "hwshqtb/ui/line_edit_t.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/gui/utf8.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/style_t.h"

namespace hwshqtb::ui {
    namespace {
        // The ONE UTF-8 decoder lives in gui/utf8.h. These forwarding names are what this file calls,
        // so the caret and the text layout can never disagree about where a code point starts, how many
        // code points a string holds, or how a malformed byte is counted.
        constexpr std::size_t sequence_length(unsigned char lead) noexcept {
            return ::hwshqtb::gui::utf8_sequence_length(lead);
        }

        constexpr std::size_t next_boundary(std::string_view text, std::size_t position) noexcept {
            return ::hwshqtb::gui::utf8_next_boundary(text, position);
        }

        constexpr std::size_t previous_boundary(std::string_view text, std::size_t position) noexcept {
            return ::hwshqtb::gui::utf8_previous_boundary(text, position);
        }

        constexpr std::size_t codepoint_count(std::string_view text) noexcept {
            return ::hwshqtb::gui::utf8_codepoint_count(text);
        }

        constexpr std::size_t codepoint_index(std::string_view text, std::size_t byte_position) noexcept {
            return ::hwshqtb::gui::utf8_codepoint_index(text, byte_position);
        }
    }

    line_edit_t::line_edit_t():
        widget_t(nullptr) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        set_opaque(true); // the frame fills the whole rectangle before the text and the caret
    }

    line_edit_t::line_edit_t(widget_t* parent):
        widget_t(parent) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
        set_opaque(true);
    }

    void line_edit_t::set_text(std::string_view text) {
        if (_text == text) {
            return;
        }
        _text.assign(text);
        _cursor = _text.size(); // the caret moves to the end, like Qt's setText
        _anchor = _cursor;
        _offset_pixels = 0;
        update(); // the text and the caret are drawn content: the frame has to repaint them
        text_changed(_text); // a state notification: no edited commit and no User event
    }

    std::string_view line_edit_t::text() const {
        return _text;
    }

    void line_edit_t::set_echo_mode(echo_mode_enum mode) {
        if (_mode == mode) {
            return;
        }
        _mode = mode;
        update(); // the displayed text changes shape: the frame has to repaint it
    }

    echo_mode_enum line_edit_t::echo_mode() const {
        return _mode;
    }

    void line_edit_t::set_read_only(bool read_only) {
        if (_read_only == read_only) {
            return;
        }
        _read_only = read_only;
        update();
    }

    bool line_edit_t::is_read_only() const {
        return _read_only;
    }

    std::size_t line_edit_t::cursor_position() const {
        return _cursor;
    }

    void line_edit_t::set_cursor_position(std::size_t position) {
        std::size_t clamped = std::min(position, _text.size());
        // Snap back to a code-point boundary, so the caret can never sit inside a multi-byte sequence.
        while (clamped > 0 && clamped < _text.size()
               && (static_cast<unsigned char>(_text[clamped]) & 0xC0) == 0x80) {
            --clamped;
        }
        const bool changed = _cursor != clamped || _anchor != clamped;
        _cursor = clamped;
        _anchor = clamped;
        if (changed) {
            update(); // the caret and the selection are drawn content
        }
    }

    bool line_edit_t::has_selection() const {
        return _cursor != _anchor;
    }

    std::string_view line_edit_t::selected_text() const {
        if (!has_selection()) {
            return {};
        }
        const std::size_t start = selection_start();
        return std::string_view{ _text }.substr(start, selection_end() - start);
    }

    void line_edit_t::select_all() {
        _anchor = 0;
        _cursor = _text.size();
        update(); // the selected run is drawn content
    }

    void line_edit_t::clear() {
        set_text({});
        _cursor = 0;
        _anchor = 0;
        _offset_pixels = 0;
        update(); // the caret moved even when the text was already empty
    }

    bool line_edit_t::text_changed() const {
        return _edited;
    }

    std::size_t line_edit_t::selection_start() const {
        return std::min(_cursor, _anchor);
    }

    std::size_t line_edit_t::selection_end() const {
        return std::max(_cursor, _anchor);
    }

    std::string line_edit_t::display_text() const {
        if (_mode == echo_mode_enum::NORMAL) {
            return _text;
        }
        return std::string(codepoint_count(_text), default_style().password_character());
    }

    std::size_t line_edit_t::cell_index(std::size_t byte_position) const {
        return _mode == echo_mode_enum::PASSWORD ? codepoint_index(_text, byte_position) : byte_position;
    }

    void line_edit_t::edit_text(std::string updated, std::size_t cursor) {
        _text = std::move(updated);
        _cursor = std::min(cursor, _text.size());
        _anchor = _cursor;
        _edited = true;
        update();            // the edited text is drawn content
        text_changed(_text); // any-source notification
        edited(_text);       // the user's edit: the ordinary commit signal
    }

    bool line_edit_t::insert_text(std::string_view input) {
        std::string accepted;
        std::size_t index = 0;
        while (index < input.size()) {
            std::size_t next = index;
            const int codepoint = ::hwshqtb::gui::utf8_next(input, next);
            // EVERY code point the input method commits is accepted, not only the ones the built-in face
            // happens to have glyphs for: a text MODEL does not decide what the FONT can draw. An
            // unrenderable code point stays in the text and the paint layer draws its fallback, exactly
            // like Qt. Only control characters are dropped — they would corrupt the single-line layout.
            if (codepoint >= 0x20 && codepoint != 0x7f) {
                accepted.append(input.substr(index, next - index));
            }
            index = next;
        }
        if (accepted.empty()) {
            return false; // nothing text-like arrived: no edit, the selection stays
        }
        std::string updated = _text;
        std::size_t position = _cursor;
        if (has_selection()) {
            const std::size_t start = selection_start();
            const std::size_t end = selection_end();
            updated.erase(start, end - start);
            position = start;
        }
        updated.insert(position, accepted);
        edit_text(std::move(updated), position + accepted.size());
        return true;
    }

    bool line_edit_t::delete_selection() {
        if (!has_selection()) {
            return false;
        }
        const std::size_t start = selection_start();
        const std::size_t end = selection_end();
        std::string updated = _text;
        updated.erase(start, end - start);
        edit_text(std::move(updated), start);
        return true;
    }

    bool line_edit_t::backspace() {
        if (delete_selection()) {
            return true;
        }
        if (_cursor == 0) {
            return false;
        }
        const std::size_t previous = previous_boundary(_text, _cursor);
        std::string updated = _text;
        updated.erase(previous, _cursor - previous);
        edit_text(std::move(updated), previous);
        return true;
    }

    bool line_edit_t::delete_forward() {
        if (delete_selection()) {
            return true;
        }
        if (_cursor >= _text.size()) {
            return false;
        }
        const std::size_t next = next_boundary(_text, _cursor);
        std::string updated = _text;
        updated.erase(_cursor, next - _cursor);
        edit_text(std::move(updated), _cursor);
        return true;
    }

    void line_edit_t::move_cursor(bool forward, bool extend) {
        const std::size_t target =
            forward ? next_boundary(_text, _cursor) : previous_boundary(_text, _cursor);
        _cursor = target;
        if (!extend) {
            _anchor = target;
        }
    }

    ::hwshqtb::gui::extent_t line_edit_t::size_hint() const {
        const style_t& style = default_style();
        const ::hwshqtb::gui::font_metrics_t metrics{};
        const int text_width = metrics.text_extent(display_text()).width();
        const int width = std::max(text_width + 2 * style.pixel_metric(pixel_metric_enum::TEXT_PADDING),
                                   style.pixel_metric(pixel_metric_enum::LINE_EDIT_WIDTH));
        const int height =
            metrics.line_height() + 2 * style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING);
        return ::hwshqtb::gui::extent_t{ width, height };
    }

    /// @brief Eight digits wide, one line tall (see the header).
    ::hwshqtb::gui::extent_t line_edit_t::minimum_size_hint() const {
        const style_t& style = default_style();
        const ::hwshqtb::gui::font_metrics_t metrics{};
        // '0' is the digit every face has, so the floor follows the resolved face rather than a constant.
        const int digit = metrics.advance('0');
        return ::hwshqtb::gui::extent_t{ 8 * digit + 2 * style.pixel_metric(pixel_metric_enum::TEXT_PADDING),
                                         size_hint().height() };
    }

    bool line_edit_t::interact(const ::hwshqtb::gui::interaction_t& in) {
        const std::size_t cursor_before = _cursor;
        const std::size_t anchor_before = _anchor;
        // edited is a PER-FRAME result: it is cleared by the next frame that drives the control.
        _edited = false;

        if (!is_enabled()) {
            return false;
        }
        if (!has_focus()) {
            return false; // the text target is the focused control only
        }

        bool consumed = false;
        if (!in.text_input.empty() && !_read_only) {
            consumed = true; // the text input is this control's, whether or not any codepoint fit
            (void)insert_text(in.text_input);
        }

        if (in.key != 0) {
            const bool shift =
                ::hwshqtb::gui::has_modifier(in.modifiers, ::hwshqtb::gui::modifier_flag::SHIFT);
            const bool control =
                ::hwshqtb::gui::has_modifier(in.modifiers, ::hwshqtb::gui::modifier_flag::CONTROL);
            const int key = in.key;
            if (key == ::hwshqtb::gui::interaction_t::left_key) {
                consumed = true;
                if (in.key_pressed) {
                    move_cursor(false, shift);
                }
            }
            else if (key == ::hwshqtb::gui::interaction_t::right_key) {
                consumed = true;
                if (in.key_pressed) {
                    move_cursor(true, shift);
                }
            }
            else if (key == ::hwshqtb::gui::interaction_t::home_key) {
                consumed = true;
                if (in.key_pressed) {
                    _cursor = 0;
                    if (!shift) {
                        _anchor = 0;
                    }
                }
            }
            else if (key == ::hwshqtb::gui::interaction_t::end_key) {
                consumed = true;
                if (in.key_pressed) {
                    _cursor = _text.size();
                    if (!shift) {
                        _anchor = _cursor;
                    }
                }
            }
            else if (key == ::hwshqtb::gui::interaction_t::backspace_key) {
                if (!_read_only) {
                    consumed = true;
                    if (in.key_pressed) {
                        (void)backspace();
                    }
                }
            }
            else if (key == ::hwshqtb::gui::interaction_t::delete_key) {
                if (!_read_only) {
                    consumed = true;
                    if (in.key_pressed) {
                        (void)delete_forward();
                    }
                }
            }
            else if (key == ::hwshqtb::gui::interaction_t::return_key) {
                consumed = true;
                if (in.key_pressed) {
                    return_pressed();
                    editing_finished();
                }
            }
            else if (key == ::hwshqtb::gui::interaction_t::escape_key) {
                consumed = true;
                if (in.key_pressed) {
                    editing_finished();
                }
            }
            else if (control && (key == 'a' || key == 'A')) {
                consumed = true;
                if (in.key_pressed) {
                    select_all();
                }
            }
        }
        if (_cursor != cursor_before || _anchor != anchor_before) {
            update(); // the caret and the selection are drawn content: repaint them
        }
        return consumed;
    }

    void line_edit_t::paint_event(paint_event_t* event) {
        ::hwshqtb::gui::painter_t& painter = event->painter();
        const style_t& style = default_style();
        const ::hwshqtb::gui::rect_t bounds = rect();
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const int vertical = style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING);
        const ::hwshqtb::gui::rect_t content{ bounds.x() + padding, bounds.y() + vertical,
                                              std::max(0, bounds.width() - 2 * padding),
                                              std::max(0, bounds.height() - 2 * vertical) };

        control_state_t state;
        state.enabled = is_enabled();
        state.focused = has_focus();
        style.draw_control(control_element_enum::LINE_EDIT_FRAME, painter, bounds, state);
        if (content.isEmpty()) {
            return;
        }

        const ::hwshqtb::gui::font_metrics_t metrics{};
        const std::string display = display_text();
        const int text_width = metrics.text_extent(display).width();
        const std::size_t caret_cell = cell_index(_cursor);
        // The caret's pen position: the advance of the code points BEFORE it. With a proportional face
        // that is the only correct answer - "caret index x one cell" was the monospaced assumption.
        const int caret_pixels =
            metrics.horizontal_advance(std::string_view{ display }.substr(0, caret_cell));

        // Keep the caret inside the content area by scrolling PIXELS: move the offset only when the caret
        // would leave [0, visible), and never scroll past the end of the text.
        const int visible = std::max(0, content.width() - 2);
        if (caret_pixels - _offset_pixels > visible) {
            _offset_pixels = caret_pixels - visible;
        }
        if (caret_pixels - _offset_pixels < 0) {
            _offset_pixels = caret_pixels;
        }
        _offset_pixels = std::clamp(_offset_pixels, 0, std::max(0, text_width - 1));
        const int offset = _offset_pixels;

        painter.save();
        painter.set_clip(content);
        if (has_selection()) {
            // Pixel interval of the selection: [prefix(start), prefix(end)), scrolled by the same offset.
            const std::size_t start = cell_index(selection_start());
            const std::size_t end = cell_index(selection_end());
            const int start_pixels = metrics.horizontal_advance(std::string_view{ display }.substr(0, start));
            const int end_pixels = metrics.horizontal_advance(std::string_view{ display }.substr(0, end));
            const ::hwshqtb::gui::rect_t highlight{ content.x() + start_pixels - offset, content.y(),
                                                    end_pixels - start_pixels, content.height() };
            style.draw_control(control_element_enum::SELECTION, painter, highlight, state);
        }
        // The style centres text in the rectangle it is given; a text-width rectangle therefore
        // LEFT-aligns it, which is what a line edit needs.
        state.text = display;
        style.draw_control(control_element_enum::TEXT, painter,
                           ::hwshqtb::gui::rect_t{ content.x() - offset, content.y(), text_width,
                                                   content.height() },
                           state);
        if (state.focused) {
            // A one-pixel caret, not blinking: this layer has no timer. Its x is the advance of the text
            // before the caret, so a wide character moves it by its own width, not by one byte.
            const ::hwshqtb::gui::rect_t caret{ content.x() + caret_pixels - offset, content.y(), 1,
                                                content.height() };
            style.draw_control(control_element_enum::LINE_EDIT_CURSOR, painter, caret, state);
        }
        painter.restore();
    }

    META_OBJECT(line_edit_t);
    META_REGISTER_WIDGET(line_edit_t);
}
