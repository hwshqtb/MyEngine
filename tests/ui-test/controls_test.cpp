// M0/M1: style_t, the keyboard convention and the first three controls.
//
// The acceptance criteria as executable checks:
//  1. label_t: size_hint follows the text (longer text -> wider and taller), the hint is the text
//     extent plus the STYLE padding, the label is not focusable, consumes no interaction and declares
//     no User property;
//  2. push_button_t: the keyboard rule (a focused Space or Return press activates exactly once, the
//     release is consumed as well, an unknown key falls through, a disabled button does nothing) and a
//     size hint derived from the style metrics;
//  3. check_box_t: press + release inside -> toggled is true for one frame and the checked state flips
//     exactly once; a release outside cancels; Space flips while focused; a disabled box neither flips
//     nor consumes;
//  4. the EXTERNAL VALUE model: a FRESH control per frame, written from the caller's own bool, shows
//     the same behaviour — the control assumes no state survives across frames;
//  5. progress_bar_t: set_value clamps into [minimum, maximum], set_range clamps and normalises the
//     bounds, value_changed fires only on a real change, and the painted chunk follows the value;
//  6. ui::im_host_t: control("check_box_t", ...) returns the toggled bool, control("label_t", ...)
//     returns the VALID, declared-only void any_t (is_valid() and NOT is_initialized()), and an unknown
//     type returns the INVALID any_t with exactly one Warning;
//  7. M2.0: the interactive controls (push button, check box, radio, slider) are StrongFocus by
//     default, the passive ones (label, progress bar) stay NoFocus;
//  8. radio_button_t: the default same-parent automatic exclusivity (selecting one clears the peer and
//     the peer emits toggled(false)), the silent setter, the "already selected" rule, opt-out via
//     set_auto_exclusive(false), the cross-parent radio_group_t, group-beats-automatic, group
//     membership/detach, and the keyboard (Space selects, arrows move selection AND focus, wrapping);
//  9. slider_t: the click jump, the continuous drag (clamped, continuous — no step alignment), the
//     press/release signals, external writes are silent, the keyboard steps/pages/Home/End, the
//     "announce only a real change" rule, a disabled slider, and the vertical orientation.
//
// Everything is offscreen through image_t + painter_t: no window and no video subsystem are created.

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "hwshqtb/gui/color_t.h"
#include "hwshqtb/gui/font_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/any_t.h"
#include "hwshqtb/metaobject/log.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/metaobject/meta_property_t.h"
#include "hwshqtb/metaobject/meta_type_t.h"
#include "hwshqtb/metaobject/object_t.h"
#include "hwshqtb/ui/check_box_t.h"
#include "hwshqtb/ui/im_host_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/label_t.h"
#include "hwshqtb/ui/progress_bar_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/radio_button_t.h"
#include "hwshqtb/ui/radio_group_t.h"
#include "hwshqtb/ui/slider_t.h"
#include "hwshqtb/ui/style_t.h"
#include "hwshqtb/ui/widget_t.h"

using namespace hwshqtb::gui;
using namespace hwshqtb::metaobject;
using namespace hwshqtb::ui;

namespace controls_test {

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

/// @brief One captured log record (the unknown-type case must log exactly one Warning).
struct record_t {
    log_level_enum level = log_level_enum::DEBUG;
    std::string message;
};

std::vector<record_t>& records() {
    static std::vector<record_t> captured;
    return captured;
}

std::size_t count_warnings(std::string_view needle) {
    std::size_t found = 0;
    for (const record_t& record : records()) {
        if (record.level == log_level_enum::WARNING && record.message.find(needle) != std::string::npos) {
            ++found;
        }
    }
    return found;
}

/// @brief The User property's current value of @p widget (false when the class declares none).
bool user_bool(widget_t& widget) {
    const meta_property_t user = widget.meta_object()->user_property();
    if (!user.is_valid()) {
        return false;
    }
    return user.read(widget).get<bool>();
}

// ---- interaction shapes ---------------------------------------------------------------------
interaction_t press(point_t local) {
    interaction_t in;
    in.mouse = local;
    in.mouse_down = true;
    in.mouse_pressed = true;
    in.hovered = true;
    return in;
}

interaction_t release(point_t local, bool inside) {
    interaction_t in;
    in.mouse = local;
    in.mouse_released = true;
    in.hovered = inside;
    return in;
}

interaction_t move(point_t local, bool down) {
    interaction_t in;
    in.mouse = local;
    in.mouse_down = down;
    in.hovered = true;
    return in;
}

interaction_t key_press(int key) {
    interaction_t in;
    in.key = key;
    in.key_pressed = true;
    return in;
}

interaction_t key_release(int key) {
    interaction_t in;
    in.key = key;
    in.key_released = true;
    return in;
}

constexpr int space = 32;
constexpr int enter = 13;
constexpr int letter_a = 65;

}  // namespace controls_test

using namespace controls_test;

int main() {
    install_log_handler([](log_level_enum level, const log_context_t&, std::string_view message) {
        records().push_back(record_t{ level, std::string(message) });
        return true; // consumed: the default stderr sink stays silent
    });
    // No application object here: register the bool the User property reads return, and the string
    // view the text_changed signal signatures name (normalized_signature() resolves the user-facing
    // spelling through the type registry).
    (void)meta_type_t::register_type<bool>();
    (void)meta_type_t::register_type<std::string_view>();

    std::printf("== M0/M1/M2: style + keyboard + label/check_box/progress_bar/radio/slider ==\n");

    // -----------------------------------------------------------------------
    // (0) The style is the single source of the metrics the controls use, and only the controls that
    //     declare a User property answer one.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        CHECK(style.pixel_metric(pixel_metric_enum::TEXT_PADDING) == 8);
        font_metrics_t metrics{};
        // A PROPERTY, not a magic value: the button band must hold one line of text plus its padding.
        // (Was `== 26`, an exact number that stops being a documented fact once the metric follows the
        // resolved face.)
        CHECK(style.pixel_metric(pixel_metric_enum::BUTTON_HEIGHT)
              >= metrics.line_height() + 2 * style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING));

        // The model is "the resolved face's line height + a style margin", so every text band has to be
        // at least one line box tall - on the built-in face and on a system face alike. These are
        // PROPERTIES: they hold whatever the face reports, and they never name a number.
        CHECK(style.pixel_metric(pixel_metric_enum::TITLE_HEIGHT) >= metrics.line_height());
        CHECK(style.pixel_metric(pixel_metric_enum::TABLE_HEADER_HEIGHT) >= metrics.line_height());
        CHECK(style.pixel_metric(pixel_metric_enum::TAB_HEIGHT) >= metrics.line_height());
        CHECK(style.pixel_metric(pixel_metric_enum::LINE_HEIGHT) >= metrics.line_height());
        CHECK(style.pixel_metric(pixel_metric_enum::BUTTON_HEIGHT) >= metrics.line_height());

        push_button_t button;
        button.set_text("Go");
        // The width follows the RESOLVED face's extent, not "bytes x one cell": a proportional face has
        // no cell, and a wide character is not its UTF-8 length.
        const int expected_width = metrics.text_extent("Go").width() + 2 * 8;
        CHECK(button.size_hint().width() == expected_width);
        CHECK(button.size_hint().height() == style.pixel_metric(pixel_metric_enum::BUTTON_HEIGHT));

        // A button refuses to be narrower than its own hint (a font derived floor): that floor is what
        // stops a layout from squeezing captions into each other.
        CHECK(button.minimum_size_hint().width() == button.size_hint().width());
        CHECK(button.minimum_size_hint().width() >= metrics.text_extent("Go").width());

        // The same for a CJK caption, spelled in UTF-8 bytes so this source file stays ASCII: the button
        // is as wide as the two characters, never as their six bytes.
        {
            push_button_t cjk_button;
            cjk_button.set_text("\xE7\xA1\xAE\xE5\xAE\x9A"); // 确定
            const int cjk_text = metrics.text_extent("\xE7\xA1\xAE\xE5\xAE\x9A").width();
            CHECK(cjk_button.size_hint().width() == cjk_text + 2 * 8);
            // Two characters, not six bytes: the text is exactly two CJK advances wide.
            CHECK(cjk_text == 2 * metrics.advance(0x4E2D));
            CHECK(cjk_button.minimum_size_hint().width() >= cjk_text);
        }

        // M2.0: the interactive controls are focusable BY DEFAULT (StrongFocus); the passive ones are
        // not (their policy stays NoFocus).
        CHECK(button.focus_policy() == focus_policy_enum::STRONG_FOCUS);
        CHECK(button.is_focusable());
        check_box_t focus_box;
        CHECK(focus_box.focus_policy() == focus_policy_enum::STRONG_FOCUS);
        radio_button_t focus_radio;
        CHECK(focus_radio.focus_policy() == focus_policy_enum::STRONG_FOCUS);
        slider_t focus_slider;
        CHECK(focus_slider.focus_policy() == focus_policy_enum::STRONG_FOCUS);
        label_t focus_label;
        CHECK(focus_label.focus_policy() == focus_policy_enum::NO_FOCUS);
        CHECK(!focus_label.is_focusable());
        progress_bar_t focus_bar;
        CHECK(focus_bar.focus_policy() == focus_policy_enum::NO_FOCUS);
        CHECK(!focus_bar.is_focusable());

        CHECK(push_button_t::static_meta_object.user_property().is_valid());
        CHECK(check_box_t::static_meta_object.user_property().is_valid());
        CHECK(!label_t::static_meta_object.user_property().is_valid());
        CHECK(!progress_bar_t::static_meta_object.user_property().is_valid());
        CHECK(check_box_t::static_meta_object.user_property().name() == "toggled");
    }

    // -----------------------------------------------------------------------
    // (0b) The style interface DEFAULTS forward to default_style(): a style that overrides neither
    //      entry measures and paints exactly like the process-wide one, so a custom style can change
    //      one thing and inherit the rest instead of restating the whole palette.
    // -----------------------------------------------------------------------
    {
        class plain_style_t final: public style_t { };

        const style_t& style = default_style();
        const plain_style_t plain;
        CHECK(plain.pixel_metric(pixel_metric_enum::TEXT_PADDING)
              == style.pixel_metric(pixel_metric_enum::TEXT_PADDING));
        CHECK(plain.pixel_metric(pixel_metric_enum::BUTTON_HEIGHT)
              == style.pixel_metric(pixel_metric_enum::BUTTON_HEIGHT));
        CHECK(plain.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS)
              == style.pixel_metric(pixel_metric_enum::SCROLL_BAR_THICKNESS));
        CHECK(plain.pixel_metric(pixel_metric_enum::LINE_HEIGHT)
              == style.pixel_metric(pixel_metric_enum::LINE_HEIGHT));
        CHECK(plain.password_character() == style.password_character());

        // The drawing entry too: the same element drawn through both styles lands on the same pixels.
        image_t direct{ 40, 26 };
        image_t forwarded{ 40, 26 };
        const control_state_t state{};
        const rect_t frame{ 0, 0, 40, 26 };
        {
            painter_t painter{ direct };
            CHECK(painter.begin());
            style.draw_control(control_element_enum::BUTTON_FRAME, painter, frame, state);
            CHECK(painter.end());
        }
        {
            painter_t painter{ forwarded };
            CHECK(painter.begin());
            plain.draw_control(control_element_enum::BUTTON_FRAME, painter, frame, state);
            CHECK(painter.end());
        }
        bool identical = true;
        for (int y = 0; y < 26 && identical; ++y) {
            for (int x = 0; x < 40; ++x) {
                if (!(direct.pixel(x, y) == forwarded.pixel(x, y))) {
                    identical = false;
                    break;
                }
            }
        }
        CHECK(identical);
    }

    // -----------------------------------------------------------------------
    // (1) label_t: the hint follows the text and comes from the style; the label is passive.
    // -----------------------------------------------------------------------
    {
        label_t label;
        CHECK(label.text().empty());
        CHECK(label.text_alignment() == text_alignment_enum::LEFT);

        const extent_t empty_hint = label.size_hint();
        label.set_text("a longer label");
        const extent_t long_hint = label.size_hint();
        CHECK(long_hint.width() > empty_hint.width());
        CHECK(long_hint.height() > empty_hint.height());

        label.set_text("Done");
        const extent_t short_hint = label.size_hint();
        CHECK(short_hint.width() < long_hint.width());

        font_metrics_t metrics{};
        const style_t& style = default_style();
        const extent_t text = metrics.text_extent("Done");
        CHECK(short_hint.width() == text.width() + 2 * style.pixel_metric(pixel_metric_enum::TEXT_PADDING));
        CHECK(short_hint.height()
              == text.height() + 2 * style.pixel_metric(pixel_metric_enum::TEXT_VERTICAL_PADDING));

        // Passive: no focus policy, no interaction, no User property.
        CHECK(label.focus_policy() == focus_policy_enum::NO_FOCUS);
        CHECK(!label.is_focusable());
        CHECK(!label.interact(press(point_t{ 4, 4 })));
        CHECK(!label.meta_object()->user_property().is_valid());

        // ---- the picture half (QLabel::setPixmap): text OR image, one content ------------------------
        CHECK(!label.has_image());
        CHECK(label.image() == nullptr);
        CHECK(!label.scaled_contents());
        {
            image_t picture{ 20, 10 };
            {
                painter_t painter{ picture };
                painter.begin();
                painter.clear(color_t::red_color());
                painter.end();
            }
            label.set_image(std::move(picture));
            CHECK(label.has_image());
            CHECK(label.image() != nullptr);
            CHECK(label.image()->size() == (extent_t{ 20, 10 }));
            // The natural size follows the CONTENT: a picture's hint is the picture.
            CHECK(label.size_hint() == (extent_t{ 20, 10 }));
            CHECK(label.minimum_size_hint() == (extent_t{ 20, 10 }));
            // A label shows one thing: taking the picture dropped the text.
            CHECK(label.text().empty());

            // Unscaled: drawn 1:1 at the top-left, so a 40x30 label shows only the image's corner.
            label.set_geometry(rect_t{ 0, 0, 40, 30 });
            image_t canvas{ 40, 30 };
            {
                painter_t painter{ canvas };
                painter.begin();
                painter.clear(color_t::white_color());
                label.render(painter);
                painter.end();
            }
            CHECK(canvas.pixel(0, 0) == color_t::red_color());
            CHECK(canvas.pixel(19, 9) == color_t::red_color());
            CHECK(canvas.pixel(20, 0) == color_t::white_color()); // right of the un-scaled picture

            // Scaled: fills the rectangle, so the picture reaches the far corner.
            label.set_scaled_contents(true);
            CHECK(label.scaled_contents());
            // (the canvas checks above carry the assertion: a scaled picture reaches the far corner)
            image_t scaled{ 40, 30 };
            {
                painter_t painter{ scaled };
                painter.begin();
                painter.clear(color_t::white_color());
                label.render(painter);
                painter.end();
            }
            CHECK(scaled.pixel(39, 29) == color_t::red_color());

            // Taking text back drops the picture, exactly like set_image() dropped the text. Checked on a
            // SEPARATE label: this one's text_changed() count belongs to the signal block below.
            label_t textual{};
            textual.set_image(std::move(canvas));
            CHECK(textual.has_image());
            textual.set_text("back to text");
            CHECK(!textual.has_image());
            CHECK(textual.image() == nullptr);
            CHECK(textual.size_hint().width() > 0);
            // set_image() cleared THIS label's text, and the signal block below expects set_text("Done")
            // to be a no-op, so put it back.
            label.set_text("Done");
            CHECK(label.text() == "Done");
        }

        // The text_changed signal fires once per real change. connect() takes the NORMALISED
        // signature, so the user-facing spelling goes through normalized_signature() first (this is
        // what resolves "std::string_view" to the identity spelling the table stores).
        int changes = 0;
        label.connect(label.meta_object()->normalized_signature("text_changed(std::string_view) const"),
                      [&changes]() { ++changes; });
        label.set_text("Done");     // same value: no signal
        CHECK(changes == 0);
        label.set_text("Other");    // real change: one signal
        CHECK(changes == 1);
        CHECK(label.text() == "Other");
    }

    // -----------------------------------------------------------------------
    // (2) push_button_t keyboard: focused Space / Return activate exactly once; other keys and a
    //     disabled button do nothing. The retained router is used, so the same convention the hosts
    //     rely on is exercised end to end.
    // -----------------------------------------------------------------------
    {
        widget_t root;
        push_button_t button{ &root };
        root.set_geometry(rect_t{ 0, 0, 80, 26 });
        button.set_geometry(rect_t{ 0, 0, 80, 26 });
        button.set_text("Go");
        int clicks = 0;
        int changes = 0;
        bool last_change = false;
        button.connect("clicked_changed(bool) const", [&clicks, &changes, &last_change](bool clicked) {
            const bool value = clicked;
            last_change = value;
            ++changes;
            if (value) {
                ++clicks;
            }
        });
        input_router_t router{ root };
        CHECK(button.is_focusable()); // StrongFocus by default: Tab and a press reach it

        // Without the focus the key is not the button's.
        CHECK(!button.interact(key_press(space)));
        CHECK(!button.clicked());
        CHECK(clicks == 0);

        router.set_focus(&button);
        CHECK(button.has_focus());

        CHECK(router.dispatch(key_press(space)));     // consumed by the focused button
        CHECK(button.clicked());
        CHECK(clicks == 1);
        CHECK(changes == 1 && last_change);           // the click edge is true
        CHECK(router.dispatch(key_release(space)));   // the release belongs to the button too
        CHECK(!button.clicked());
        CHECK(clicks == 1);
        CHECK(changes == 2 && !last_change);          // the clear edge is false

        CHECK(router.dispatch(key_press(enter)));     // Return activates as well
        CHECK(button.clicked());
        CHECK(clicks == 2);
        CHECK(changes == 3 && last_change);
        (void)router.dispatch(key_release(enter));
        CHECK(changes == 4 && !last_change);

        CHECK(!router.dispatch(key_press(letter_a))); // an unknown key falls through
        CHECK(!button.clicked());
        CHECK(clicks == 2);
        CHECK(changes == 4);                          // no extra change

        // A disabled button holds no focus (the router drops it) and ignores a Space press.
        button.set_enabled(false);
        (void)router.dispatch(key_press(letter_a));   // the router notices the focus is gone
        CHECK(!button.has_focus());
        CHECK(!button.interact(key_press(space)));
        CHECK(!button.clicked());
        CHECK(clicks == 2);
        CHECK(changes == 4);
    }

    // -----------------------------------------------------------------------
    // (2b) CLICK FOCUS with the real controls: a press focuses the STRONG_FOCUS button (and still
    //      reaches it as a normal click), while a press on the NO_FOCUS label leaves the focus alone.
    // -----------------------------------------------------------------------
    {
        widget_t root;
        push_button_t button{ &root };
        label_t label{ &root };
        root.set_geometry(rect_t{ 0, 0, 120, 60 });
        button.set_geometry(rect_t{ 0, 0, 120, 26 });
        label.set_geometry(rect_t{ 0, 30, 120, 26 });
        button.set_text("Go");
        int clicks = 0;
        button.connect("clicked_changed(bool) const", [&clicks](bool clicked) {
            if (clicked) {
                ++clicks;
            }
        });
        input_router_t router{ root };
        CHECK(router.focus_item() == nullptr);

        CHECK(router.dispatch(press(point_t{ 6, 13 })));      // the button consumes the press ...
        CHECK(router.focus_item() == &button);                // ... and the focus moved first
        CHECK(button.has_focus());
        CHECK(router.dispatch(release(point_t{ 6, 13 }, true)));
        CHECK(clicks == 1);                                   // the click still completed
        CHECK(router.mouse_target() == nullptr);              // the release ended the grab

        CHECK(label.focus_policy() == focus_policy_enum::NO_FOCUS);
        CHECK(!router.dispatch(press(point_t{ 6, 43 })));     // a label consumes nothing ...
        CHECK(router.focus_item() == &button);                // ... and changes no focus
        CHECK(!label.has_focus());
    }

    // -----------------------------------------------------------------------
    // (3) check_box_t: click state machine, per-frame toggled, outside release, disabled state.
    // -----------------------------------------------------------------------
    {
        widget_t root;
        check_box_t box{ &root };
        root.set_geometry(rect_t{ 0, 0, 120, 26 });
        box.set_geometry(rect_t{ 0, 0, 120, 26 });
        box.set_text("Check");
        CHECK(box.focus_policy() == focus_policy_enum::STRONG_FOCUS);
        CHECK(!box.is_checked());
        CHECK(!box.toggled());

        // Press: consumed, nothing flipped yet.
        CHECK(box.interact(press(point_t{ 6, 13 })));
        CHECK(!box.toggled());
        CHECK(!box.is_checked());
        CHECK(!user_bool(box));
        // Release inside: flipped once and reported for exactly this frame.
        CHECK(box.interact(release(point_t{ 6, 13 }, true)));
        CHECK(box.toggled());
        CHECK(user_bool(box));
        CHECK(box.is_checked());
        // The next driven frame clears the per-frame result, not the state.
        CHECK(!box.interact(move(point_t{ 6, 13 }, false)));
        CHECK(!box.toggled());
        CHECK(!user_bool(box));
        CHECK(box.is_checked());

        // Release outside cancels: the press produced no flip.
        CHECK(box.interact(press(point_t{ 6, 13 })));
        CHECK(box.interact(release(point_t{ 400, 13 }, false)));
        CHECK(!box.toggled());
        CHECK(box.is_checked());   // unchanged by the cancelled press

        // Disabled: consumes nothing and never flips.
        const bool before = box.is_checked();
        box.set_enabled(false);
        CHECK(!box.interact(press(point_t{ 6, 13 })));
        CHECK(!box.interact(release(point_t{ 6, 13 }, true)));
        CHECK(!box.toggled());
        CHECK(box.is_checked() == before);
        box.set_enabled(true);

        // toggled(bool) reports the new state; text_changed fires once per real change.
        bool last_toggled = false;
        int toggles = 0;
        box.connect("toggled(bool) const", [&last_toggled, &toggles](bool checked) {
            ++toggles;
            last_toggled = checked;
        });
        int text_changes = 0;
        box.connect(box.meta_object()->normalized_signature("text_changed(std::string_view) const"),
                    [&text_changes]() { ++text_changes; });
        box.set_text("Check");   // same value: no signal
        CHECK(text_changes == 0);
        box.set_text("Checked");
        CHECK(text_changes == 1);
        const bool target = !box.is_checked();
        (void)box.interact(press(point_t{ 6, 13 }));
        (void)box.interact(release(point_t{ 6, 13 }, true));
        CHECK(toggles == 1);
        CHECK(last_toggled == target);
        CHECK(box.is_checked() == target);
    }

    // -----------------------------------------------------------------------
    // (4) THE EXTERNAL VALUE MODEL: the caller owns checked, writes it every frame, and reads only
    //     toggled. A FRESH control per frame behaves identically — nothing has to survive.
    // -----------------------------------------------------------------------
    {
        bool external = false;
        {
            // Frame 1: a fresh control, written from the caller's variable, then clicked.
            check_box_t box;
            box.set_text("Persist me");
            box.set_checked(external);
            CHECK(box.is_checked() == external);
            (void)box.interact(press(point_t{ 6, 13 }));
            (void)box.interact(release(point_t{ 6, 13 }, true));
            CHECK(box.toggled());
            if (box.toggled()) {
                external = !external;   // the CALLER owns the new value
            }
        }   // the control dies here: the value must survive OUTSIDE it
        CHECK(external);

        {
            // Frame 2: a brand-new control, fed by the caller; the same driving code works.
            check_box_t box;
            box.set_text("Persist me");
            box.set_checked(external);
            CHECK(box.is_checked());
            CHECK(!box.toggled());      // no event on a frame without input
            CHECK(!box.interact(move(point_t{ 6, 13 }, false)));
            CHECK(!box.toggled());
            CHECK(box.is_checked() == external);
        }

        {
            // Frame 3: the caller decides the value changed elsewhere (a model update); the control
            // shows it without any interaction and reports no toggled event of its own.
            check_box_t box;
            external = false;
            box.set_checked(external);
            CHECK(!box.is_checked());
            CHECK(!box.toggled());
        }
    }

    // -----------------------------------------------------------------------
    // (5) Keyboard for the check box: focused Space flips once; without the focus or with another key
    //     it does not. The router is the focus authority (the same rule the button follows).
    // -----------------------------------------------------------------------
    {
        widget_t root;
        check_box_t box{ &root };
        root.set_geometry(rect_t{ 0, 0, 120, 26 });
        box.set_geometry(rect_t{ 0, 0, 120, 26 });
        box.set_text("Space");
        input_router_t router{ root };

        CHECK(!box.interact(key_press(space)));   // no focus: not the box's key
        CHECK(!box.is_checked());

        router.set_focus(&box);
        CHECK(box.has_focus());
        CHECK(router.dispatch(key_press(space)));
        CHECK(box.is_checked());
        CHECK(box.toggled());
        CHECK(router.dispatch(key_release(space)));// the release is consumed too
        CHECK(!box.toggled());
        CHECK(box.is_checked());

        CHECK(!router.dispatch(key_press(letter_a))); // another key never flips it
        CHECK(box.is_checked());

        (void)box.interact(move(point_t{ 6, 13 }, false)); // clear the frame result
        CHECK(router.dispatch(key_press(space)));
        CHECK(!box.is_checked());                  // flipped back
        CHECK(box.toggled());
        (void)router.dispatch(key_release(space));
    }

    // -----------------------------------------------------------------------
    // (6) progress_bar_t: the clamp invariant, the normalised range and the change signal.
    // -----------------------------------------------------------------------
    {
        progress_bar_t bar;
        CHECK(bar.minimum() == 0);
        CHECK(bar.maximum() == 100);
        CHECK(bar.value() == 0);
        CHECK(bar.direction() == orientation_enum::HORIZONTAL);
        CHECK(bar.focus_policy() == focus_policy_enum::NO_FOCUS); // a passive control
        CHECK(bar.size_hint().height() == 16 + 2 * 1);

        bar.set_value(150);
        CHECK(bar.value() == 100);                 // clamped to maximum
        bar.set_value(-5);
        CHECK(bar.value() == 0);                   // clamped to minimum

        int changes = 0;
        bar.connect("value_changed(int) const", [&changes]() { ++changes; });
        bar.set_value(0);                          // no real change: no signal
        CHECK(changes == 0);
        bar.set_value(40);
        CHECK(changes == 1);
        bar.set_value(400);                        // clamped to 100: a real change, one signal
        CHECK(bar.value() == 100);
        CHECK(changes == 2);

        bar.set_range(10, 20);
        CHECK(bar.minimum() == 10);
        CHECK(bar.maximum() == 20);
        CHECK(bar.value() == 20);                  // 100 clamps down into the new range
        bar.set_value(5);
        CHECK(bar.value() == 10);
        bar.set_value(25);
        CHECK(bar.value() == 20);

        bar.set_range(30, 10);                     // inverted: the bounds are swapped
        CHECK(bar.minimum() == 10);
        CHECK(bar.maximum() == 30);
        CHECK(bar.value() == 20);

        bar.set_range(5, 5);                       // an empty range: the value is pinned to it
        CHECK(bar.minimum() == 5);
        CHECK(bar.maximum() == 5);
        CHECK(bar.value() == 5);
    }

    // -----------------------------------------------------------------------
    // (7) The painted chunk follows the value (the style owns the colours, so the test compares
    //     pixels with each other instead of with hard-coded colours).
    // -----------------------------------------------------------------------
    {
        image_t image{ 120, 18 };
        painter_t painter{ image };
        painter.begin();
        progress_bar_t bar;
        bar.set_geometry(rect_t{ 0, 0, 120, 18 });

        painter.clear(color_t::white_color());
        bar.set_range(0, 100);
        bar.set_value(0);
        bar.render(painter);
        const color_t empty_left = image.pixel(10, 9);
        const color_t empty_right = image.pixel(100, 9);
        CHECK(empty_left == empty_right);          // no chunk: one groove fill

        painter.clear(color_t::white_color());
        bar.set_value(100);
        bar.render(painter);
        const color_t full_left = image.pixel(10, 9);
        const color_t full_right = image.pixel(100, 9);
        CHECK(full_left == full_right);            // full: the chunk covers the inner groove
        CHECK(full_left != empty_left);            // ... in a different colour than the groove

        painter.clear(color_t::white_color());
        bar.set_value(50);
        bar.render(painter);
        CHECK(image.pixel(10, 9) == full_left);    // left half: chunk
        CHECK(image.pixel(100, 9) == empty_right); // right half: groove
        painter.end();
    }

    // -----------------------------------------------------------------------
    // (8) The immediate host: the toggled bool, the VALID void any_t of a control without a User
    //     property, and the INVALID any_t of a failed lookup.
    // -----------------------------------------------------------------------
    {
        image_t image{ 200, 120 };
        painter_t painter{ image };
        painter.begin();
        im_host_t host;
        host.set_geometry(rect_t{ 0, 0, 200, 120 });

        any_t label_value;
        any_t box_value;
        host.set_frame_handler([&label_value, &box_value](im_host_t& driven) {
            label_value = driven.control("label_t", "l");
            box_value = driven.control("check_box_t", "cb");
        });
        host.render(painter);

        CHECK(label_value.is_valid());
        CHECK(!label_value.is_initialized());
        CHECK(label_value.type().is_void());
        CHECK(box_value.is_valid());
        CHECK(box_value.is_initialized());
        CHECK(!box_value.get<bool>());

        widget_t* const label = host.widget("label_t", "l");
        widget_t* const box = host.widget("check_box_t", "cb");
        CHECK(label != nullptr);
        CHECK(box != nullptr);
        if (box != nullptr) {
            const rect_t row = box->geometry();     // the row control() gave the box
            const point_t click{ row.x() + 6, row.y() + row.height() / 2 };
            host.interact(press(click));
            host.interact(release(click, true));
            host.render(painter);                    // I2: consumption and paint back to back
            CHECK(box_value.is_valid());
            CHECK(box_value.is_initialized());
            CHECK(box_value.get<bool>());
            CHECK(static_cast<check_box_t*>(box)->is_checked());

            host.render(painter);                    // the settle snapshot clears the frame result
            CHECK(box_value.is_initialized());
            CHECK(!box_value.get<bool>());

            // The SAME Space rule through the immediate host: its second-level focus receives the key
            // step and the control toggles — one implementation, two hosts.
            auto* const box_control = static_cast<check_box_t*>(box);
            box_control->set_focus();               // stops at the host's own focus sink
            CHECK(box_control->has_focus());
            const bool before_key = box_control->is_checked();
            CHECK(host.interact(key_press(space))); // the host collects the key for its inner focus
            host.render(painter);
            CHECK(box_value.is_initialized());
            CHECK(box_value.get<bool>());
            CHECK(box_control->is_checked() != before_key);
            host.render(painter);                    // settle: no event this pass
            CHECK(!box_value.get<bool>());
        }

        // An unknown type is a REAL failure: the invalid any_t, and exactly one Warning.
        records().clear();
        any_t missing;
        host.set_frame_handler([&missing](im_host_t& driven) {
            missing = driven.control("no_such_type_xyz", "missing");
        });
        host.render(painter);
        CHECK(!missing.is_valid());
        CHECK(!missing.is_initialized());
        CHECK(count_warnings("no class registered") == 1u);
        painter.end();
    }

    // -----------------------------------------------------------------------
    // (9) radio_button_t: the default same-parent automatic exclusivity, the silent setter and the
    //     "already selected" rule.
    // -----------------------------------------------------------------------
    {
        widget_t root;
        radio_button_t a{ &root };
        radio_button_t b{ &root };
        a.set_text("A");
        b.set_text("B");
        root.set_geometry(rect_t{ 0, 0, 160, 60 });
        a.set_geometry(rect_t{ 0, 0, 160, 26 });
        b.set_geometry(rect_t{ 0, 30, 160, 26 });
        CHECK(a.focus_policy() == focus_policy_enum::STRONG_FOCUS);
        CHECK(a.is_auto_exclusive());
        CHECK(!a.is_checked());
        CHECK(!b.is_checked());
        CHECK(a.group() == nullptr);

        int a_true = 0;
        int a_false = 0;
        int b_true = 0;
        int b_false = 0;
        a.connect("toggled(bool) const", [&a_true, &a_false](bool checked) {
            (checked ? a_true : a_false) += 1;
        });
        b.connect("toggled(bool) const", [&b_true, &b_false](bool checked) {
            (checked ? b_true : b_false) += 1;
        });

        // Click A: A selected, its per-frame event raised, B untouched (it was not checked).
        CHECK(a.interact(press(point_t{ 6, 13 })));
        CHECK(a.interact(release(point_t{ 6, 13 }, true)));
        CHECK(a.is_checked());
        CHECK(!b.is_checked());
        CHECK(a.toggled());
        CHECK(user_bool(a));                    // the User property is readable, same value
        CHECK(!b.toggled());                    // a peer's frame event is NOT raised by someone else's action
        CHECK(a_true == 1 && a_false == 0 && b_true == 0 && b_false == 0);

        // The next driven frame clears A's frame event.
        CHECK(!a.interact(move(point_t{ 6, 13 }, false)));
        CHECK(!a.toggled());

        // Click B: B selected and A CLEARED with toggled(false) — the peer change is announced.
        CHECK(b.interact(press(point_t{ 6, 13 })));
        CHECK(b.interact(release(point_t{ 6, 13 }, true)));
        CHECK(b.is_checked());
        CHECK(!a.is_checked());
        CHECK(b.toggled());
        CHECK(b_true == 1 && a_false == 1 && a_true == 1);
        CHECK(!a.toggled());                    // still no per-frame result on the peer

        // Clicking the already selected B keeps it selected and emits nothing (Qt).
        CHECK(b.interact(press(point_t{ 6, 13 })));
        CHECK(b.interact(release(point_t{ 6, 13 }, true)));
        CHECK(b.is_checked());
        CHECK(b_true == 1);
        CHECK(!b.toggled());

        // The setter announces its own state change (and clears the exclusive peers) but never
        // raises a per-frame result.
        b.set_checked(false);
        CHECK(!b.is_checked());
        CHECK(b_false == 1);                    // the setter's own toggled(false)
        CHECK(!b.toggled());                    // ... and no User event
        a.set_checked(true);
        CHECK(a.is_checked());
        CHECK(a_true == 2);                     // the click's signal plus the setter's
        CHECK(b_false == 1);
        CHECK(!a.toggled());
        a.set_checked(true);                    // already checked: nothing happens at all
        CHECK(a_true == 2);
        b.set_checked(true);                    // clears A (peer change announced) + its own signal
        CHECK(b.is_checked());
        CHECK(!a.is_checked());
        CHECK(a_false == 2);
        CHECK(b_true == 2);
        CHECK(!b.toggled());

        // set_auto_exclusive(false) opts both out: they stop clearing each other.
        a.set_auto_exclusive(false);
        b.set_auto_exclusive(false);
        a.set_checked(true);
        CHECK(a.is_checked() && b.is_checked());
        a.set_checked(false);
        b.set_checked(false);
        CHECK(a.interact(press(point_t{ 6, 13 })));
        CHECK(a.interact(release(point_t{ 6, 13 }, true)));
        CHECK(a.is_checked() && !b.is_checked());
        CHECK(b.interact(press(point_t{ 6, 13 })));
        CHECK(b.interact(release(point_t{ 6, 13 }, true)));
        CHECK(b.is_checked() && a.is_checked()); // exclusivity off: both stay checked
    }

    // -----------------------------------------------------------------------
    // (10) radio_group_t: the explicit group spans parents, wins over the same-parent automatic
    //      exclusivity, can be turned non-exclusive, and both directions of detach are clean.
    // -----------------------------------------------------------------------
    {
        widget_t parent_a;
        widget_t parent_b;
        radio_button_t a{ &parent_a };
        radio_button_t b{ &parent_b };
        radio_button_t sibling{ &parent_a };    // same parent as A, NOT in the group
        parent_a.set_geometry(rect_t{ 0, 0, 160, 60 });
        parent_b.set_geometry(rect_t{ 0, 0, 160, 60 });
        int b_false = 0;
        b.connect("toggled(bool) const", [&b_false](bool checked) {
            if (!checked) {
                ++b_false;
            }
        });

        radio_group_t group;
        CHECK(group.is_exclusive());
        group.add_button(&a);
        group.add_button(&b);
        CHECK(group.button_count() == 2);
        CHECK(a.group() == &group);
        CHECK(b.group() == &group);
        CHECK(group.checked_button() == nullptr);

        // Cross-parent exclusion: selecting A clears B (different parent).
        CHECK(a.interact(press(point_t{ 6, 13 })));
        CHECK(a.interact(release(point_t{ 6, 13 }, true)));
        CHECK(a.is_checked());
        CHECK(!b.is_checked());
        CHECK(b_false == 0);                    // B was not checked before, so nothing to announce
        CHECK(group.checked_button() == &a);

        // B checked first; selecting A clears it and announces toggled(false).
        b.set_checked(true);                    // clears A (group), silent for B
        CHECK(b.is_checked() && !a.is_checked());
        b.set_checked(true);                    // no change
        CHECK(a.interact(press(point_t{ 6, 13 })));
        CHECK(a.interact(release(point_t{ 6, 13 }, true)));
        CHECK(a.is_checked() && !b.is_checked());
        CHECK(b_false == 1);

        // The group beats the same-parent set: selecting the ungrouped sibling does NOT clear A, and
        // the group-owned A does not clear the sibling either.
        CHECK(sibling.interact(press(point_t{ 6, 13 })));
        CHECK(sibling.interact(release(point_t{ 6, 13 }, true)));
        CHECK(sibling.is_checked());
        CHECK(a.is_checked());
        CHECK(a.interact(press(point_t{ 6, 13 })));   // A already checked: no-op
        CHECK(a.is_checked() && sibling.is_checked());

        // A non-exclusive group lets both members be checked.
        group.set_exclusive(false);
        CHECK(!group.is_exclusive());
        b.set_checked(true);
        CHECK(a.is_checked() && b.is_checked());
        CHECK(group.checked_button() == &a);    // first checked in membership order

        // remove_button detaches; the group no longer constrains or reports the button.
        group.remove_button(&b);
        CHECK(group.button_count() == 1);
        CHECK(b.group() == nullptr);
        CHECK(group.checked_button() == &a);
        group.add_button(nullptr);              // ignored
        CHECK(group.button_count() == 1);

        // Destroying a group detaches its remaining buttons (no dangling group pointer).
        {
            radio_group_t temporary;
            temporary.add_button(&b);
            CHECK(b.group() == &temporary);
        }
        CHECK(b.group() == nullptr);
        CHECK(a.group() == &group);

        // A button that leaves the group falls back to its automatic same-parent set.
        group.remove_button(&a);
        a.set_auto_exclusive(true);
        b.set_auto_exclusive(true);
        a.set_checked(false);
        b.set_checked(false);
        a.set_checked(true);
        CHECK(a.is_checked());
        CHECK(!b.is_checked());                 // B has a DIFFERENT parent: the automatic set never touches it
        b.set_checked(true);
        CHECK(b.is_checked() && a.is_checked()); // still independent: different parents
    }

    // -----------------------------------------------------------------------
    // (11) radio_button_t keyboard: Space selects; the arrows move the selection AND the focus among
    //      the automatic siblings, wrapping; a group-owned button has no automatic set.
    // -----------------------------------------------------------------------
    {
        widget_t root;
        radio_button_t a{ &root };
        radio_button_t b{ &root };
        radio_button_t c{ &root };
        a.set_text("A");
        b.set_text("B");
        c.set_text("C");
        root.set_geometry(rect_t{ 0, 0, 160, 90 });
        a.set_geometry(rect_t{ 0, 0, 160, 26 });
        b.set_geometry(rect_t{ 0, 30, 160, 26 });
        c.set_geometry(rect_t{ 0, 60, 160, 26 });
        input_router_t router{ root };

        // Without the focus none of the keys is the radio's.
        CHECK(!a.interact(key_press(interaction_t::space_key)));
        CHECK(!a.interact(key_press(interaction_t::right_key)));
        CHECK(!a.is_checked());

        router.set_focus(&a);
        CHECK(a.has_focus());
        CHECK(router.dispatch(key_press(interaction_t::space_key)));
        CHECK(a.is_checked());
        CHECK(a.toggled());
        CHECK(router.dispatch(key_release(interaction_t::space_key)));
        CHECK(!a.toggled());                    // the release frame cleared the per-frame result

        // Right arrow: B is selected and receives the focus.
        CHECK(router.dispatch(key_press(interaction_t::right_key)));
        CHECK(b.is_checked());
        CHECK(!a.is_checked());
        CHECK(b.has_focus());
        CHECK(!a.has_focus());
        CHECK(router.focus_item() == &b);
        (void)router.dispatch(key_release(interaction_t::right_key));

        // Down: C; Right wraps to A; Left wraps backwards to C.
        CHECK(router.dispatch(key_press(interaction_t::down_key)));
        CHECK(c.is_checked() && c.has_focus());
        (void)router.dispatch(key_release(interaction_t::down_key));
        CHECK(router.dispatch(key_press(interaction_t::right_key)));
        CHECK(a.is_checked() && a.has_focus());
        (void)router.dispatch(key_release(interaction_t::right_key));
        CHECK(router.dispatch(key_press(interaction_t::left_key)));
        CHECK(c.is_checked() && c.has_focus());
        (void)router.dispatch(key_release(interaction_t::left_key));

        // Disabled: consumes nothing and never selects.
        c.set_enabled(false);
        const bool was_checked = a.is_checked();
        CHECK(!c.interact(key_press(interaction_t::space_key)));
        CHECK(!c.interact(press(point_t{ 6, 13 })));
        CHECK(!c.interact(release(point_t{ 6, 13 }, true)));
        CHECK(c.is_checked());                  // disabling changes no state by itself
        CHECK(a.is_checked() == was_checked);
    }

    {
        // A group-owned button has no automatic set: the arrows fall through (the group wins).
        widget_t root;
        radio_button_t a{ &root };
        radio_button_t b{ &root };
        root.set_geometry(rect_t{ 0, 0, 160, 60 });
        a.set_geometry(rect_t{ 0, 0, 160, 26 });
        b.set_geometry(rect_t{ 0, 30, 160, 26 });
        radio_group_t group;
        group.add_button(&a);
        group.add_button(&b);
        input_router_t router{ root };
        router.set_focus(&a);
        CHECK(a.has_focus());
        CHECK(!a.interact(key_press(interaction_t::right_key))); // not consumed: no automatic set
        CHECK(!b.is_checked());
        CHECK(a.has_focus());
        // Space still selects it (the group clears the other member, nothing is announced: B was off).
        CHECK(a.interact(key_press(interaction_t::space_key)));
        CHECK(a.is_checked());
        CHECK(a.group() == &group);
    }

    // -----------------------------------------------------------------------
    // (12) slider_t: the click jump, the continuous drag, the clamp and the press/release signals.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        slider_t slider;
        slider.set_geometry(rect_t{ 0, 0, 120, 18 });
        CHECK(slider.focus_policy() == focus_policy_enum::STRONG_FOCUS);
        CHECK(slider.minimum() == 0 && slider.maximum() == 100 && slider.value() == 0);
        CHECK(slider.single_step() == 1 && slider.page_step() == 10);
        CHECK(slider.orientation() == orientation_enum::HORIZONTAL);
        CHECK(slider.size_hint()
              == extent_t{ style.pixel_metric(pixel_metric_enum::SLIDER_LENGTH),
                           style.pixel_metric(pixel_metric_enum::SLIDER_HANDLE_SIZE) });
        CHECK(!slider.value_changed());

        int changes = 0;
        int last_value = -1;
        int presses = 0;
        int releases = 0;
        slider.connect("value_changed(int) const", [&changes, &last_value](int value) {
            ++changes;
            last_value = value;
        });
        slider.connect("slider_pressed() const", [&presses]() { ++presses; });
        slider.connect("slider_released() const", [&releases]() { ++releases; });

        // Press at the far right: jump to the maximum, then the press/change are announced.
        CHECK(slider.interact(press(point_t{ 114, 9 })));
        CHECK(slider.value() == 100);
        CHECK(presses == 1);
        CHECK(changes == 1);
        CHECK(last_value == 100);
        CHECK(slider.value_changed());          // the User event
        CHECK(user_bool(slider));

        // Drag to the middle: 50 (the track is 108 px, the handle 12, so x = 60 maps to 54/108).
        CHECK(slider.interact(move(point_t{ 60, 9 }, true)));
        CHECK(slider.value() == 50);
        CHECK(changes == 2);
        // Dragged far outside the widget: clamped, still tracked.
        CHECK(slider.interact(move(point_t{ -50, 9 }, true)));
        CHECK(slider.value() == 0);
        CHECK(changes == 3);
        // Release: one last (unchanged) update plus slider_released.
        CHECK(slider.interact(release(point_t{ -50, 9 }, true)));
        CHECK(releases == 1);
        CHECK(slider.value() == 0);
        CHECK(changes == 3);

        // The next driven frame clears the User event.
        CHECK(!slider.interact(move(point_t{ 60, 9 }, false)));
        CHECK(!slider.value_changed());

        // A press at the left edge changes nothing, but the press/release are still announced.
        CHECK(slider.interact(press(point_t{ 6, 9 })));
        CHECK(presses == 2);
        CHECK(changes == 3);
        CHECK(!slider.value_changed());
        CHECK(slider.interact(release(point_t{ 6, 9 }, true)));
        CHECK(releases == 2);

        // External writes emit value_changed on a real change (a state notification) and never raise
        // the User event; the range normalises and clamps the value.
        slider.set_value(1000);
        CHECK(slider.value() == 100);
        CHECK(changes == 4);
        CHECK(!slider.value_changed());
        slider.set_value(-10);
        CHECK(slider.value() == 0);
        CHECK(changes == 5);
        slider.set_range(10, 20);
        CHECK(slider.minimum() == 10 && slider.maximum() == 20);
        CHECK(slider.value() == 10);            // clamped from 0
        CHECK(changes == 6);
        slider.set_range(30, 10);               // inverted: the bounds are swapped, no value change
        CHECK(slider.minimum() == 10 && slider.maximum() == 30);
        CHECK(slider.value() == 10);
        CHECK(changes == 6);

        // Disabled: consumes nothing and never changes.
        slider.set_value(20);
        slider.set_enabled(false);
        CHECK(!slider.interact(press(point_t{ 114, 9 })));
        CHECK(!slider.interact(move(point_t{ 114, 9 }, true)));
        CHECK(!slider.interact(release(point_t{ 114, 9 }, true)));
        CHECK(slider.value() == 20);
        CHECK(presses == 2 && releases == 2);
    }

    // -----------------------------------------------------------------------
    // (13) slider_t keyboard: steps, pages, Home/End; only a real change emits and raises the event;
    //      the vertical orientation maps bottom=minimum, top=maximum.
    // -----------------------------------------------------------------------
    {
        const style_t& style = default_style();
        slider_t slider;
        slider.set_geometry(rect_t{ 0, 0, 120, 18 });
        input_router_t router{ slider };        // the slider is its own root item

        // Without the focus the keys are not the slider's.
        CHECK(!slider.interact(key_press(interaction_t::right_key)));
        CHECK(slider.value() == 0);

        router.set_focus(&slider);
        CHECK(slider.has_focus());
        int changes = 0;
        slider.connect("value_changed(int) const", [&changes]() { ++changes; });
        slider.set_value(50);                   // a real change: one state signal, no User event
        CHECK(changes == 1);
        CHECK(!slider.value_changed());

        CHECK(router.dispatch(key_press(interaction_t::right_key)));
        CHECK(slider.value() == 51);
        CHECK(changes == 2);
        CHECK(slider.value_changed());
        (void)router.dispatch(key_release(interaction_t::right_key));

        CHECK(router.dispatch(key_press(interaction_t::page_up_key)));
        CHECK(slider.value() == 61);
        CHECK(changes == 3);
        (void)router.dispatch(key_release(interaction_t::page_up_key));

        CHECK(router.dispatch(key_press(interaction_t::page_down_key)));
        CHECK(slider.value() == 51);
        CHECK(changes == 4);
        (void)router.dispatch(key_release(interaction_t::page_down_key));

        CHECK(router.dispatch(key_press(interaction_t::end_key)));
        CHECK(slider.value() == 100);
        CHECK(changes == 5);
        (void)router.dispatch(key_release(interaction_t::end_key));

        // At the maximum the key is consumed, but nothing changes and nothing is announced.
        CHECK(router.dispatch(key_press(interaction_t::right_key)));
        CHECK(slider.value() == 100);
        CHECK(changes == 5);
        CHECK(!slider.value_changed());
        (void)router.dispatch(key_release(interaction_t::right_key));

        CHECK(router.dispatch(key_press(interaction_t::home_key)));
        CHECK(slider.value() == 0);
        CHECK(changes == 6);
        (void)router.dispatch(key_release(interaction_t::home_key));
        CHECK(router.dispatch(key_press(interaction_t::down_key)));
        CHECK(slider.value() == 0);             // at the minimum: no change
        CHECK(changes == 6);
        (void)router.dispatch(key_release(interaction_t::down_key));

        // The steps are configurable; Left/Down decrement and Right/Up increment them.
        slider.set_single_step(5);
        slider.set_page_step(25);
        CHECK(slider.single_step() == 5 && slider.page_step() == 25);
        CHECK(router.dispatch(key_press(interaction_t::up_key)));
        CHECK(slider.value() == 5);
        (void)router.dispatch(key_release(interaction_t::up_key));
        CHECK(router.dispatch(key_press(interaction_t::page_up_key)));
        CHECK(slider.value() == 30);
        (void)router.dispatch(key_release(interaction_t::page_up_key));
        CHECK(router.dispatch(key_press(interaction_t::left_key)));
        CHECK(slider.value() == 25);
        (void)router.dispatch(key_release(interaction_t::left_key));

        // Vertical: the minimum sits at the bottom and the maximum at the top.
        slider.set_orientation(orientation_enum::VERTICAL);
        CHECK(slider.orientation() == orientation_enum::VERTICAL);
        CHECK(slider.size_hint()
              == extent_t{ style.pixel_metric(pixel_metric_enum::SLIDER_HANDLE_SIZE),
                           style.pixel_metric(pixel_metric_enum::SLIDER_LENGTH) });
        slider.set_geometry(rect_t{ 0, 0, 18, 120 });
        slider.set_value(50);
        CHECK(slider.interact(press(point_t{ 9, 114 })));   // bottom: minimum
        CHECK(slider.value() == 0);
        CHECK(slider.interact(release(point_t{ 9, 114 }, true)));
        CHECK(slider.interact(press(point_t{ 9, 6 })));     // top: maximum
        CHECK(slider.value() == 100);
        CHECK(slider.interact(release(point_t{ 9, 6 }, true)));
    }

    install_log_handler(log_handler{});

    std::printf("controls: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
