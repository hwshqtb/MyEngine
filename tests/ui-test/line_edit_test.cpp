// M4: the text-input channel and line_edit_t.
//
// The acceptance criteria as executable checks:
//  1. input_state_t accumulates the delivered UTF-8 text and reset_text_input()/reset_frame() clear
//     it; the retained router delivers a text step to the FOCUS item (not to the item under the
//     pointer) and the immediate host replays it into its controls;
//  2. line_edit_t: set_text is a state notification only (text_changed, never the edited commit/User
//     event); typing inserts (replacing a selection), Backspace/Delete are boundary- and
//     multi-byte-safe, the caret moves with/without Shift, Home/End, Ctrl+A, Enter/Esc signals,
//     read-only, disabled, password mode (one placeholder per character), clear(), set_cursor_position
//     clamping, size_hint;
//  3. the User property `text_changed` is true only on a frame the USER edited the text; `edited` is
//     the ordinary commit signal.
//
// Offscreen only (image_t + painter_t); no window and no video subsystem are created.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/font_database_t.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/input_state_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/log.h"
#include "hwshqtb/metaobject/macros.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/meta_property_t.h"
#include "hwshqtb/metaobject/meta_type_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/metaobject/register_object.h"
#include "hwshqtb/ui/im_host_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/line_edit_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace line_edit_test {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        std::printf("  [FAIL] %s (line %d)\n", what, line);
        ++g_failures;
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

bool user_bool(widget_t& widget) {
    const meta_property_t user = widget.meta_object()->user_property();
    if (!user.is_valid()) {
        return false;
    }
    return user.read(widget).get<bool>();
}

std::uint32_t shift_bits() {
    return modifier_bits(modifier_flag::SHIFT);
}

std::uint32_t control_bits() {
    return modifier_bits(modifier_flag::CONTROL);
}

interaction_t key_press(int key, std::uint32_t modifiers = 0) {
    interaction_t in;
    in.key = key;
    in.key_pressed = true;
    in.modifiers = modifiers;
    return in;
}

interaction_t key_release(int key, std::uint32_t modifiers = 0) {
    interaction_t in;
    in.key = key;
    in.key_released = true;
    in.modifiers = modifiers;
    return in;
}

interaction_t text_step(std::string text) {
    interaction_t in;
    in.text_input = std::move(text);
    return in;
}

interaction_t move(point_t local) {
    interaction_t in;
    in.mouse = local;
    in.hovered = true;
    return in;
}

interaction_t press_click(point_t local) {
    interaction_t in = move(local);
    in.mouse_down = true;
    in.mouse_pressed = true;
    return in;
}

/// @brief A probe that records the text-input steps it is handed and never consumes them.
/// @note Registered so the immediate host can create it by name; STRONG_FOCUS so the retained router
///       can focus it (text input goes to the FOCUS item, not to the item under the pointer).
class text_probe_t final: public widget_t {
    OBJECT

public:
    text_probe_t():
        widget_t(nullptr) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
    }

    explicit text_probe_t(widget_t* parent):
        widget_t(parent) {
        set_focus_policy(focus_policy_enum::STRONG_FOCUS);
    }

    static inline std::string last_text;
    static inline int text_steps = 0;

    static void reset() {
        last_text.clear();
        text_steps = 0;
    }

    extent_t size_hint() const override {
        return extent_t{ 40, 20 };
    }

    bool interact(const interaction_t& in) override {
        if (in.key == 0 && !in.text_input.empty()) {
            last_text = in.text_input;
            ++text_steps;
        }
        return false; // not consumed: the step stays observable without a click
    }

protected:
    void paint_event(paint_event_t*) override { }
};
META_OBJECT(text_probe_t);
META_REGISTER_TYPE(text_probe_t);

/// @brief One line edit wired into a root and a router (the caret-focused state the tests drive).
struct edit_env_t {
    widget_t root;
    line_edit_t edit{ &root };
    input_router_t router{ root };
    int changed = 0;
    int edited = 0;
    int returns = 0;
    int finished = 0;
    std::string last_changed;
    std::string last_edited;

    edit_env_t() {
        root.set_geometry(rect_t{ 0, 0, 200, 40 });
        edit.set_geometry(rect_t{ 0, 0, 140, 16 });
        edit.connect(edit.meta_object()->normalized_signature("text_changed(std::string_view) const"),
                     [this](std::string_view text) {
                         ++changed;
                         last_changed = std::string(text);
                     });
        edit.connect(edit.meta_object()->normalized_signature("edited(std::string_view) const"),
                     [this](std::string_view text) {
                         ++edited;
                         last_edited = std::string(text);
                     });
        edit.connect("return_pressed() const", [this]() { ++returns; });
        edit.connect("editing_finished() const", [this]() { ++finished; });
        router.set_focus(&edit);
    }
};

/// @brief Whether any pixel of @p area differs between two images.
bool region_differs(const image_t& first, const image_t& second, rect_t area) {
    for (int y = area.top(); y < area.bottom(); ++y) {
        for (int x = area.left(); x < area.right(); ++x) {
            if (!(first.pixel(x, y) == second.pixel(x, y))) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace line_edit_test

using namespace line_edit_test;

int main() {
    pin_builtin_font_family(); // deterministic text: the built-in face, not the machine's UI font
    install_log_handler([](log_level_enum, const log_context_t&, std::string_view) {
        return true; // consumed: the default stderr sink stays silent
    });
    (void)meta_type_t::register_type<bool>();
    (void)meta_type_t::register_type<std::string_view>();

    std::printf("== M4: text input + line_edit_t ==\n");

    // -----------------------------------------------------------------------
    // (1) The text-input channel: the input state's accumulator, the retained router (focus target)
    //     and the immediate host's replay.
    // -----------------------------------------------------------------------
    {
        input_state_t state;
        CHECK(state.text_input().empty());
        state.add_text_input("ab");
        state.add_text_input("cd");
        CHECK(state.text_input() == "abcd");
        state.add_wheel(point_t{ 1, 0 });
        state.reset_text_input();       // clears the text only
        CHECK(state.text_input().empty());
        CHECK(state.wheel_delta() == point_t{ 1, 0 });
        state.add_text_input("xy");
        state.reset_frame();            // ... and the frame reset clears it too
        CHECK(state.text_input().empty());

        // (1a) RETAINED: the text step goes to the FOCUS item, even with the pointer elsewhere.
        widget_t root;
        text_probe_t probe{ &root };
        root.set_geometry(rect_t{ 0, 0, 100, 100 });
        probe.set_geometry(rect_t{ 0, 0, 40, 20 });
        input_router_t router{ root };
        text_probe_t::reset();
        interaction_t in = text_step("ab");
        in.mouse = point_t{ 5, 5 };     // over the probe, but the probe is not focused
        CHECK(!router.dispatch(in));
        CHECK(text_probe_t::text_steps == 0);
        router.set_focus(&probe);
        in.mouse = point_t{ 500, 500 }; // outside the whole tree: the focus target still gets it
        CHECK(!router.dispatch(in));
        CHECK(text_probe_t::text_steps == 1);
        CHECK(text_probe_t::last_text == "ab");
        (void)router.dispatch(in);      // a second step is delivered independently
        CHECK(text_probe_t::text_steps == 2);

        // (1b) IMMEDIATE: the batch replay copies the step's text into the control's input.
        image_t image{ 100, 60 };
        painter_t painter{ image };
        painter.begin();
        im_host_t host;
        host.set_geometry(rect_t{ 0, 0, 100, 60 });
        host.set_frame_handler([](im_host_t& driven) { (void)driven.control("text_probe_t", "p"); });
        host.render(painter);
        text_probe_t::reset();
        CHECK(host.interact(text_step("cd")));
        host.render(painter);
        CHECK(text_probe_t::text_steps == 1);
        CHECK(text_probe_t::last_text == "cd");
        painter.end();

        // (1c) CLICK FOCUS, end to end: a press on the line edit focuses it, and the very next text
        // step lands in it — the "click, then type" path a real user takes (no programmatic
        // set_focus() in between).
        edit_env_t clicked;
        clicked.router.set_focus(nullptr);
        CHECK(clicked.router.focus_item() == nullptr);
        CHECK(!clicked.edit.has_focus());
        CHECK(!clicked.router.dispatch(press_click(point_t{ 5, 5 }))); // a line edit ignores the mouse
        CHECK(clicked.router.focus_item() == &clicked.edit);
        CHECK(clicked.edit.has_focus());
        CHECK(clicked.router.dispatch(text_step("OK")));
        CHECK(clicked.edit.text() == "OK");
        CHECK(clicked.edit.cursor_position() == 2);
        CHECK(clicked.edited == 1);
        CHECK(clicked.last_edited == "OK");
    }

    // -----------------------------------------------------------------------
    // (2) line_edit_t: the state, the setter's signals and the natural size.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const int vertical = style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING);
        const font_metrics_t metrics{};
        CHECK(style.pixel_metric(pixel_metric_enum::LINE_EDIT_WIDTH) == 80);
        CHECK(style.password_character() == '*');

        edit_env_t env;
        line_edit_t& edit = env.edit;
        CHECK(edit.focus_policy() == focus_policy_enum::STRONG_FOCUS);
        CHECK(edit.text().empty());
        CHECK(edit.cursor_position() == 0);
        CHECK(!edit.has_selection());
        CHECK(edit.echo_mode() == echo_mode_enum::NORMAL);
        CHECK(!edit.is_read_only());
        CHECK(line_edit_t::static_meta_object.user_property().is_valid());
        CHECK(line_edit_t::static_meta_object.user_property().name() == "text_changed");
        CHECK(edit.size_hint()
              == extent_t{ style.pixel_metric(pixel_metric_enum::LINE_EDIT_WIDTH),
                           metrics.line_height() + 2 * vertical });

        // set_text: a state notification only.
        edit.set_text("abc");
        CHECK(edit.text() == "abc");
        CHECK(edit.cursor_position() == 3);       // the caret moves to the end
        CHECK(env.changed == 1 && env.last_changed == "abc");
        CHECK(env.edited == 0);                   // NEVER the edited commit from the setter
        CHECK(!env.edit.text_changed());          // ... and never the User event
        CHECK(!user_bool(edit));
        edit.set_text("abc");                     // no real change: no signal
        CHECK(env.changed == 1);
        edit.set_text("abcdefghijklmn");           // 14 glyphs: wider than the LINE_EDIT_WIDTH floor
        CHECK(env.changed == 2);
        CHECK(edit.size_hint().width() == 14 * metrics.advance(' ') + 2 * padding);
        edit.set_text("abc");
    }

    // -----------------------------------------------------------------------
    // (3) Typing, inserting in the middle, replacing a selection and the boundary deletes.
    // -----------------------------------------------------------------------
    {
        edit_env_t env;
        line_edit_t& edit = env.edit;
        edit.set_text("abc");
        CHECK(edit.interact(text_step("de")));
        CHECK(edit.text() == "abcde");
        CHECK(edit.cursor_position() == 5);
        CHECK(env.changed == 2);
        CHECK(env.edited == 1 && env.last_edited == "abcde");
        CHECK(edit.text_changed());
        CHECK(user_bool(edit));
        CHECK(!edit.interact(move(point_t{ 1, 1 }))); // the next driven frame clears the event
        CHECK(!edit.text_changed());
        CHECK(env.edited == 1);

        // The text channel is the ONLY source of characters: a printable key code is not text.
        // line_edit_t must not look for the keyboard itself, so the key changes nothing, moves no
        // caret, raises no signal and is not consumed (consuming it would claim a key the focus chain
        // may still need).
        const int changed_before_key = env.changed;
        const int edited_before_key = env.edited;
        CHECK(!edit.interact(key_press('z')));
        CHECK(edit.text() == "abcde");
        CHECK(edit.cursor_position() == 5);
        CHECK(!edit.interact(key_release('z')));
        CHECK(edit.text() == "abcde");
        CHECK(env.changed == changed_before_key && env.edited == edited_before_key);
        CHECK(!edit.text_changed());

        // Insert in the middle.
        edit.set_cursor_position(2);
        CHECK(edit.interact(text_step("X")));
        CHECK(edit.text() == "abXcde");
        CHECK(edit.cursor_position() == 3);

        // An input method commits WHOLE code points and the control accepts every one of them: the FONT
        // decides what it can draw, not what the text model may hold. This used to be filtered down to
        // "printable ASCII only", so a Chinese commit was dropped and the IME looked broken.
        edit.set_text("");
        CHECK(edit.interact(text_step("\xE9\x92\xB1")));           // 钱, three bytes, one code point
        CHECK(edit.text() == "\xE9\x92\xB1");
        CHECK(edit.cursor_position() == 3);                        // the caret is a BYTE index
        CHECK(edit.interact(text_step("ab")));
        CHECK(edit.text() == "\xE9\x92\xB1" "ab");
        CHECK(edit.cursor_position() == 5);
        // Editing works on code-point boundaries: one Backspace removes the whole character, never a
        // third of it.
        CHECK(edit.interact(key_press(interaction_t::backspace_key)));
        CHECK(edit.text() == "\xE9\x92\xB1" "a");
        CHECK(edit.interact(key_press(interaction_t::backspace_key)));
        CHECK(edit.text() == "\xE9\x92\xB1");
        CHECK(edit.cursor_position() == 3);
        CHECK(edit.interact(key_press(interaction_t::backspace_key)));
        CHECK(edit.text().empty());
        CHECK(edit.cursor_position() == 0);
        // Control characters are the one thing that is NOT text: they would corrupt the single-line layout.
        // The step is still this control's (it owns the text channel while it has the focus), but it
        // inserts nothing.
        CHECK(edit.interact(text_step("\x01")));
        CHECK(edit.text().empty());
        CHECK(edit.cursor_position() == 0);

        // Shift+Right twice selects "he"; typing replaces the selection.
        edit.set_text("hello");
        edit.set_cursor_position(0);
        CHECK(edit.interact(key_press(interaction_t::right_key, shift_bits())));
        (void)edit.interact(key_release(interaction_t::right_key, shift_bits()));
        CHECK(edit.interact(key_press(interaction_t::right_key, shift_bits())));
        (void)edit.interact(key_release(interaction_t::right_key, shift_bits()));
        CHECK(edit.has_selection());
        CHECK(edit.selected_text() == "he");
        CHECK(edit.interact(text_step("XY")));
        CHECK(edit.text() == "XYllo");
        CHECK(edit.cursor_position() == 2);
        CHECK(!edit.has_selection());

        // Backspace/Delete at the boundaries: consumed (editable) but no change, no signals.
        const int changed_before = env.changed;
        const int edited_before = env.edited;
        edit.set_cursor_position(0);
        CHECK(edit.interact(key_press(interaction_t::backspace_key)));
        CHECK(edit.text() == "XYllo");
        edit.set_cursor_position(edit.text().size());
        CHECK(edit.interact(key_press(interaction_t::delete_key)));
        CHECK(edit.text() == "XYllo");
        CHECK(env.changed == changed_before && env.edited == edited_before);
        // A real delete in the middle still works.
        edit.set_cursor_position(2);
        CHECK(edit.interact(key_press(interaction_t::backspace_key)));
        CHECK(edit.text() == "Xllo");
    }

    // -----------------------------------------------------------------------
    // (4) Multi-byte safety: the caret snaps to code-point boundaries and one delete removes one
    //     whole sequence.
    // -----------------------------------------------------------------------
    {
        edit_env_t env;
        line_edit_t& edit = env.edit;
        const std::string text = std::string{ 'a' } + "\xC3\xA9" + 'b'; // a, U+00E9, b (4 bytes)
        CHECK(text.size() == 4);
        edit.set_text(text);
        CHECK(edit.text().size() == 4);
        edit.set_cursor_position(2);              // inside the two-byte sequence
        CHECK(edit.cursor_position() == 1);       // ... snapped back to its start
        CHECK(edit.interact(key_press(interaction_t::delete_key)));
        CHECK(edit.text() == "ab");
        CHECK(edit.cursor_position() == 1);

        edit.set_text(text);
        edit.set_cursor_position(3);              // after the sequence's second byte
        CHECK(edit.cursor_position() == 3);
        CHECK(edit.interact(key_press(interaction_t::backspace_key)));
        CHECK(edit.text() == "ab");
        CHECK(edit.cursor_position() == 1);
    }

    // -----------------------------------------------------------------------
    // (5) The caret keys, Ctrl+A and the Enter/Escape signals.
    // -----------------------------------------------------------------------
    {
        edit_env_t env;
        line_edit_t& edit = env.edit;
        edit.set_text("abc");
        edit.set_cursor_position(3);
        CHECK(edit.interact(key_press(interaction_t::left_key)));
        CHECK(edit.cursor_position() == 2);
        (void)edit.interact(key_release(interaction_t::left_key));
        CHECK(edit.interact(key_press(interaction_t::home_key)));
        CHECK(edit.cursor_position() == 0);
        (void)edit.interact(key_release(interaction_t::home_key));
        CHECK(edit.interact(key_press(interaction_t::end_key)));
        CHECK(edit.cursor_position() == 3);
        (void)edit.interact(key_release(interaction_t::end_key));
        CHECK(edit.interact(key_press('a', control_bits()))); // Ctrl+A
        CHECK(edit.has_selection());
        CHECK(edit.selected_text() == "abc");
        (void)edit.interact(key_release('a', control_bits()));

        CHECK(edit.interact(key_press(interaction_t::return_key)));
        CHECK(env.returns == 1 && env.finished == 1);
        (void)edit.interact(key_release(interaction_t::return_key));
        CHECK(edit.interact(key_press(interaction_t::escape_key)));
        CHECK(env.returns == 1 && env.finished == 2);
        (void)edit.interact(key_release(interaction_t::escape_key));
    }

    // -----------------------------------------------------------------------
    // (6) Password mode: one placeholder per character (a multi-byte character is one character),
    //     the stored text stays intact, and the drawing differs from the normal mode.
    // -----------------------------------------------------------------------
    {
        edit_env_t env;
        line_edit_t& edit = env.edit;
        const style_t& style = default_style();
        const font_metrics_t metrics{};
        const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
        const int advance = metrics.advance(' ');
        std::string many;
        for (int index = 0; index < 20; ++index) {
            many += "\xC3\xA9"; // 20 characters, 40 bytes
        }
        edit.set_text(many);
        const int normal_width = edit.size_hint().width();
        // One glyph per CODE POINT, not per byte: a two-byte character occupies one cell, so the normal
        // layout is as wide as the password layout and only the drawn glyphs differ.
        CHECK(normal_width == 20 * advance + 2 * padding);
        edit.set_echo_mode(echo_mode_enum::PASSWORD);
        CHECK(edit.echo_mode() == echo_mode_enum::PASSWORD);
        CHECK(edit.text() == many);                            // the stored text is untouched
        const int password_width = edit.size_hint().width();
        CHECK(password_width == 20 * advance + 2 * padding);   // one placeholder per CHARACTER
        CHECK(password_width == normal_width);                 // same cells: the mask changes ink only

        // The glyphs differ: normal "ab" vs the masked "**".
        image_t image{ 180, 24 };
        painter_t painter{ image };
        painter.begin();
        line_edit_t drawn;
        drawn.set_geometry(rect_t{ 0, 0, 180, 24 });
        drawn.set_text("ab");
        painter.clear(color_t::white_color());
        drawn.render(painter);
        image_t normal{ 180, 24 };
        painter_t normal_painter{ normal };
        normal_painter.begin();
        drawn.set_echo_mode(echo_mode_enum::NORMAL);
        normal_painter.clear(color_t::white_color());
        drawn.render(normal_painter);
        normal_painter.end();
        image_t masked{ 180, 24 };
        painter_t masked_painter{ masked };
        masked_painter.begin();
        drawn.set_echo_mode(echo_mode_enum::PASSWORD);
        masked_painter.clear(color_t::white_color());
        drawn.render(masked_painter);
        masked_painter.end();
        CHECK(region_differs(normal, masked, rect_t{ padding, 2, 3 * advance, 14 }));
        painter.end();
    }

    // -----------------------------------------------------------------------
    // (7) Read-only and disabled.
    // -----------------------------------------------------------------------
    {
        edit_env_t env;
        line_edit_t& edit = env.edit;
        edit.set_text("abc");
        edit.set_read_only(true);
        CHECK(edit.is_read_only());
        CHECK(!edit.interact(text_step("X")));    // editing input is not consumed
        CHECK(edit.text() == "abc");
        CHECK(edit.interact(key_press(interaction_t::left_key))); // navigation still works
        CHECK(edit.cursor_position() == 2);
        CHECK(edit.interact(key_press('a', control_bits())));     // ... and so does the selection
        CHECK(edit.selected_text() == "abc");
        CHECK(!edit.interact(key_press(interaction_t::backspace_key)));
        CHECK(!edit.interact(key_press(interaction_t::delete_key)));
        CHECK(edit.text() == "abc");
        CHECK(edit.interact(key_press(interaction_t::return_key))); // Enter is not an edit
        CHECK(env.finished == 1);

        edit.set_read_only(false);
        edit.set_enabled(false);
        CHECK(!edit.interact(text_step("X")));
        CHECK(!edit.interact(key_press(interaction_t::left_key)));
        CHECK(edit.text() == "abc");
    }

    // -----------------------------------------------------------------------
    // (8) clear(), the caret clamp and the caret/selection drawing through the style.
    // -----------------------------------------------------------------------
    {
        edit_env_t env;
        line_edit_t& edit = env.edit;
        edit.set_text("abc");
        edit.clear();
        CHECK(edit.text().empty());
        CHECK(edit.cursor_position() == 0);
        CHECK(env.changed == 2);                  // set_text("abc") + clear()
        CHECK(env.edited == 0);
        edit.set_cursor_position(99);
        CHECK(edit.cursor_position() == 0);       // clamped to the (empty) text

        edit.set_text("hello");
        edit.set_cursor_position(99);
        CHECK(edit.cursor_position() == 5);

        // The selection highlight uses the style's SELECTION element; painting must not crash and the
        // selected area must differ from the unselected one.
        image_t plain{ 140, 16 };
        painter_t plain_painter{ plain };
        plain_painter.begin();
        edit.set_geometry(rect_t{ 0, 0, 140, 16 });
        edit.select_all();
        edit.set_cursor_position(5);              // collapse the selection
        plain_painter.clear(color_t::white_color());
        edit.render(plain_painter);
        plain_painter.end();
        image_t selected{ 140, 16 };
        painter_t selected_painter{ selected };
        selected_painter.begin();
        edit.select_all();
        selected_painter.clear(color_t::white_color());
        edit.render(selected_painter);
        selected_painter.end();
        const style_t& style = default_style();
        CHECK(region_differs(plain, selected,
                             rect_t{ style.pixel_metric(pixel_metric_enum::TEXT_PADDING), 2,
                                     5 * font_metrics_t{}.advance(' '), 12 }));
    }

    // -----------------------------------------------------------------------
    // (9) The immediate host, end to end: inner focus + text step + the `text_changed` User property.
    // -----------------------------------------------------------------------
    {
        image_t image{ 200, 40 };
        painter_t painter{ image };
        painter.begin();
        im_host_t host;
        host.set_geometry(rect_t{ 0, 0, 200, 40 });
        any_t value;
        host.set_frame_handler([&value](im_host_t& driven) { value = driven.control("line_edit_t", "le"); });
        host.render(painter);
        auto* const edit = static_cast<line_edit_t*>(host.widget("line_edit_t", "le"));
        CHECK(edit != nullptr);
        CHECK(value.is_initialized());
        CHECK(!value.get<bool>());
        edit->set_focus();                        // the host's own focus sink takes it
        CHECK(edit->has_focus());
        CHECK(host.interact(text_step("hi")));
        host.render(painter);
        CHECK(edit->text() == "hi");
        CHECK(value.is_initialized());
        CHECK(value.get<bool>());                 // the frame's User event
        host.render(painter);                      // the settle snapshot clears it
        CHECK(!value.get<bool>());
        CHECK(edit->text() == "hi");
        painter.end();
    }

    install_log_handler(log_handler{});

    // -----------------------------------------------------------------------
    // (10) The caret lives in PIXELS, not in cells: its x is the advance of the text before it, so a
    //      wide character moves it by its own width. This is the round's core regression - the old model
    //      multiplied a CELL INDEX by one advance, which a proportional face does not have.
    // -----------------------------------------------------------------------
    {
        const font_metrics_t metrics{};
        // One code point (中) advances by exactly one advance - never by its three UTF-8 bytes, which is
        // what "index x cell" effectively did for a byte-indexed caret.
        CHECK(metrics.horizontal_advance("\xE4\xB8\xAD") == metrics.advance(0x4E2D));
        CHECK(metrics.horizontal_advance("\xE4\xB8\xAD\xE4\xB8\xAD")
              == 2 * metrics.horizontal_advance("\xE4\xB8\xAD"));
        CHECK(metrics.horizontal_advance("ab") == metrics.advance('a') + metrics.advance('b'));
        // Under the pinned built-in face that advance is the old 6, so nothing about the old numbers moved.
        if (hwshqtb::gui::font_database_t::instance().default_family()
            == hwshqtb::gui::font_database_t::builtin_family()) {
            CHECK(metrics.horizontal_advance("\xE4\xB8\xAD") == 6);
        }

        // The caret's column is the advance of the text before it: that expression IS the implementation,
        // so the assertions above pin the formula. Its PIXEL position is proven separately, on a face
        // where the old and the new model disagree (see the section below).
        // What the caret must NOT do is advance per byte: two code points of 3 bytes each move it by
        // 2 * advance, which the second assertion above already pins.
    }

    // -----------------------------------------------------------------------
    // (11) PIXEL PROOF: the caret column is the prefix advance, not "cells x space".
    //      A proportional face is what separates the two answers (they differ by ~24 px here), so the
    //      section runs on the system face and skips when this build has none.
    // -----------------------------------------------------------------------
    {
        const std::string_view builtin_family_name = hwshqtb::gui::font_database_t::builtin_family();
        // An empty family means "the platform default", i.e. the system UI face.
        hwshqtb::gui::font_database_t::instance().set_default_family(std::string_view{});
        const std::string_view face = hwshqtb::gui::font_database_t::instance().default_family();
        if (face == builtin_family_name) {
            std::printf("[line_edit] no system font face: the caret-column section is skipped\n");
        }
        else {
            const font_metrics_t metrics{};
            const style_t& style = default_style();
            const int padding = style.pixel_metric(pixel_metric_enum::TEXT_PADDING);
            const int vertical = style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING);

            // The env has a root and a router, and the router installs the root's focus sink: that is what
            // makes item_t::set_focus() from the leaf actually take effect.
            edit_env_t env;
            env.edit.set_text("\xE4\xB8\xAD\xE6\x96\x87"); // 中文: setText leaves the caret at the end

            // The router may already hold the first focusable item, so clear the focus EXPLICITLY: the two
            // frames must differ by the caret and nothing else.
            env.router.set_focus(nullptr);
            CHECK(!env.edit.has_focus());
            image_t unfocused{ 200, 40 };
            env.root.update();
            env.edit.update();
            {
                painter_t painter{ unfocused };
                painter.begin();
                // A DARK canvas: the theme's caret colour is light, so on white it would be invisible and
                // the two frames would compare equal for the wrong reason.
                painter.clear(color_t::black_color());
                env.root.render(painter);
                painter.end();
            }

            env.router.set_focus(&env.edit);
            CHECK(env.edit.has_focus());
            image_t focused{ 200, 40 };
            env.root.update();
            env.edit.update();
            {
                painter_t painter{ focused };
                painter.begin();
                painter.clear(color_t::black_color());
                env.root.render(painter);
                painter.end();
            }

            // Focus changes exactly two things: the frame colour and the caret. The frame is the 1 px
            // border, so scanning the CONTENT area leaves the caret as the only difference, and its
            // left-most column is the caret's x.
            const rect_t bounds = env.edit.rect();
            const int content_left = bounds.x() + padding;            const int content_right = bounds.right() - padding;
            int caret_x = -1;
            for (int x = content_left; x < content_right && caret_x < 0; ++x) {
                for (int y = bounds.y() + vertical; y < bounds.bottom() - vertical && caret_x < 0; ++y) {
                    if (!(focused.pixel(x, y) == unfocused.pixel(x, y))) {
                        caret_x = x;
                    }
                }
            }
            CHECK(caret_x >= 0);
            {
                int ink_a = 0;
                int ink_b = 0;
                int diff = 0;
                for (int y = 0; y < 40; ++y) {
                    for (int x = 0; x < 200; ++x) {
                        if (!(unfocused.pixel(x, y) == color_t::white_color())) {
                            ++ink_a;
                        }
                        if (!(focused.pixel(x, y) == color_t::white_color())) {
                            ++ink_b;
                        }
                        if (!(focused.pixel(x, y) == unfocused.pixel(x, y))) {
                            ++diff;
                        }
                    }
                }
                std::printf("[line_edit] frames: %d pixel(s) changed by focus, edit rect %dx%d\n", diff,
                            bounds.width(), bounds.height());
            }
            const int prefix = metrics.horizontal_advance("\xE4\xB8\xAD\xE6\x96\x87");
            const int old_model = 2 * metrics.advance(' ');
            const int delta = caret_x - (content_left + prefix);
            CHECK(delta >= -1 && delta <= 1);
            // The monospaced answer is a DIFFERENT column on this face: both models cannot be right.
            CHECK(prefix != old_model);
            CHECK(caret_x != content_left + old_model);
            // No scrolling: the text is far narrower than the content area, so the offset stays 0.
            CHECK(prefix < content_right - content_left);
            std::printf("[line_edit] caret column: x=%d, prefix advance=%d (content left %d), "
                        "old cell model=%d\n", caret_x, prefix, content_left, content_left + old_model);
        }
        // Restore determinism for anything that might follow.
        pin_builtin_font_family();
    }

    std::printf("line_edit: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
