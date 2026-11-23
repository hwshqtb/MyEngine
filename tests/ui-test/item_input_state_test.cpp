// Item-level pointer state: hover / press / capture lives on the ITEM, so the ONE input session
// writes it and every view reads the same values back.
//
// The acceptance criteria as executable checks:
//  1. hover: set_hovered(true) sets is_hovered(), records damage and fires hover_event() once; the
//     same value again records NO damage and fires nothing; false clears it the same way;
//  2. press: the same per button (set_pressed), with LEFT and RIGHT as independent states;
//  3. capture: an item with no pointer sink cannot capture; with a router's sink it becomes the
//     router's single target, and release_pointer() clears both sides;
//  4. routing: a press records is_pressed(button) and captures the pointer on the item it was
//     delivered to; a release far away still reaches that item, then the press and the capture clear;
//  5. hover migration: moving from A to B clears A and sets B, and leaving the tree clears the last;
//  6. disabled / not-visible items never enter hover, neither directly nor through the router;
//  7. the paint path reads the ITEM state: a push_button_t paints the hover/press colours from
//     is_hovered() / is_pressed(), not from a private copy;
//  8. the immediate host mirrors the hover/press it computed into the control's item state, so the
//     state matches the press/drag/release it actually drove;
//  9. the window adapter clears the pointer state when the mouse leaves the window (the real native
//     filter, SDL's dummy video driver — the same setup the damage test uses);
// 10. a NaN / +-inf / +-1e30 pointer coordinate reaches the input state as a DEFINITE point (the
//     window saturates instead of casting the float, which would be undefined behaviour), while a
//     legitimate coordinate keeps the pixel it always had;
// 11. the window adapter enables the platform text input while the window has the keyboard focus and
//     disables it on focus loss, hide() and close(); SDL_TextInputActive() is the witness, so the
//     check fails if the SDL_StartTextInput() call is removed.
//
// Everything is offscreen; sections 9, 10 and 11 are the only ones that create a window, under SDL's
// dummy video driver.

#include <cstdio>
#include <limits>
#include <string_view>

#include <SDL3/SDL.h>

#include "hwshqtb/gui/geometry.h"
#include "hwshqtb/gui/gui_application_t.h"
#include "hwshqtb/gui/image_t.h"
#include "hwshqtb/gui/interaction_t.h"
#include "hwshqtb/gui/painter_t.h"
#include "hwshqtb/metaobject/core_application.h"
#include "hwshqtb/metaobject/event_dispatcher.h"
#include "hwshqtb/metaobject/meta_object_t.h"
#include "hwshqtb/ui/im_host_t.h"
#include "hwshqtb/ui/input_router_t.h"
#include "hwshqtb/ui/push_button_t.h"
#include "hwshqtb/ui/widget_t.h"
#include "hwshqtb/ui/widget_window_t.h"

using namespace hwshqtb::gui;
using hwshqtb::ui::im_host_t;
using hwshqtb::ui::input_router_t;
using hwshqtb::ui::push_button_t;
using hwshqtb::ui::widget_t;
using hwshqtb::ui::widget_window_t;

namespace item_input_state_test {

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

/// A widget that records what it was told. It derives from widget_t without an OBJECT table (the
/// documented test-fixture way); the two pointer hooks keep the base repaint request.
class probe_widget_t final: public widget_t {
public:
    using widget_t::widget_t;

    bool interact(const interaction_t& in) override {
        ++interactions;
        last = in;
        return consume;
    }

    void hover_event(::hwshqtb::ui::hover_event_t* event) override {
        ++hover_changes;
        last_hovered = event->hovered();
        ::hwshqtb::ui::item_t::hover_event(event); // keep the default repaint request
    }

    void press_event(::hwshqtb::ui::press_event_t* event) override {
        ++press_changes;
        last_button = event->button();
        last_pressed = event->pressed();
        ::hwshqtb::ui::item_t::press_event(event);
    }

    /// @brief Whether interact() consumes what it receives.
    bool consume = false;
    /// @brief How many interactions (including the synthetic hover-out) reached this widget.
    int interactions = 0;
    /// @brief The last interaction it received.
    interaction_t last{};
    /// @brief How many hover_event() calls fired.
    int hover_changes = 0;
    /// @brief The argument of the last hover_event() call.
    bool last_hovered = false;
    /// @brief How many press_event() calls fired.
    int press_changes = 0;
    /// @brief The button of the last press_event() call.
    mouse_button_enum last_button = mouse_button_enum::LEFT;
    /// @brief The state of the last press_event() call.
    bool last_pressed = false;
};

/// The reflected type name of push_button_t (naming it also keeps its translation unit linked, which
/// is what registers the type in a static build).
const std::string_view button_type = push_button_t::static_meta_object.class_name();

/// A pointer step at @p p with no edges (a move).
interaction_t move_to(point_t p) {
    interaction_t in;
    in.mouse = p;
    return in;
}

/// The press edge of the LEFT button at @p p, with the button still held.
interaction_t press_at(point_t p) {
    interaction_t in = move_to(p);
    in.button = mouse_button_enum::LEFT;
    in.mouse_down = true;
    in.mouse_pressed = true;
    return in;
}

/// The release edge of the LEFT button at @p p.
interaction_t release_at(point_t p) {
    interaction_t in = move_to(p);
    in.button = mouse_button_enum::LEFT;
    in.mouse_released = true;
    return in;
}

/// Paint @p widget into a fresh image and return one pixel of its content.
color_t paint_pixel(widget_t& widget, int x, int y) {
    const rect_t bounds = widget.rect();
    image_t device{ extent_t{ bounds.width(), bounds.height() } };
    painter_t painter{ device };
    painter.begin();
    widget.render(painter);
    painter.end();
    return device.pixel(x, y);
}

/// @brief Pump the dispatcher until @p predicate holds.
/// @param predicate The condition to wait for.
/// @param rounds The round cap; the dispatcher pumps ONE system event per round.
/// @return Whether the predicate held within @p rounds rounds.
/// @note A few rounds are enough: the pushed event is behind at most the window's own lifecycle
///       events, and the dummy video driver produces only a handful.
template <typename Predicate>
bool pump_until(Predicate predicate, int rounds = 64) {
    for (int round = 0; round < rounds; ++round) {
        if (predicate()) {
            return true;
        }
        (void)hwshqtb::metaobject::event_dispatcher::instance().process_events();
    }
    return predicate();
}

/// @brief Let the dispatcher consume everything already queued.
/// @param rounds The number of pump rounds; one round takes ONE system event.
/// @note Used where waiting for a value would be ambiguous: two injected cases can expect the same
///       point, so "the state already holds it" proves nothing about the event just pushed. Pumping a
///       fixed number of rounds guarantees the event was consumed before the assertion reads it.
void pump_queued(int rounds = 32) {
    for (int round = 0; round < rounds; ++round) {
        (void)hwshqtb::metaobject::event_dispatcher::instance().process_events();
    }
}

/// @brief Assert that the pointer landed on @p expected, naming the injected case on failure.
/// @param what The case, e.g. "NaN y".
/// @param got The position the window's input state holds.
/// @param expected The point the saturation rule requires.
/// @param line The CHECK line to report.
void check_point(const char* what, point_t got, point_t expected, int line) {
    ++g_checks;
    if (!(got == expected)) {
        std::printf("  [FAIL] %s: (%d, %d) != (%d, %d) (line %d)\n", what, got.x(), got.y(),
            expected.x(), expected.y(), line);
        ++g_failures;
    }
}

#define CHECK_POINT(what, got, expected) check_point((what), (got), (expected), __LINE__)

}  // namespace item_input_state_test

using namespace item_input_state_test;  // the file-local test namespace stays the file's vocabulary

int main(int argc, char** argv) {
    std::printf("== Item pointer state (hover / press / capture) ==\n");

    // -----------------------------------------------------------------------
    // (1) Hover: the state, the damage, the hook, and the no-change case.
    // -----------------------------------------------------------------------
    {
        probe_widget_t widget;
        widget.set_geometry(rect_t{ 0, 0, 100, 40 });
        widget.clear_damage(); // set_geometry() itself requests a repaint; start from a clean record
        CHECK(!widget.is_hovered());
        CHECK(widget.damage().is_empty());

        widget.set_hovered(true);
        CHECK(widget.is_hovered());
        CHECK(!widget.damage().is_empty()); // the change requested a repaint
        CHECK(widget.hover_changes == 1);
        CHECK(widget.last_hovered);

        widget.clear_damage();
        widget.set_hovered(true); // the same value again
        CHECK(widget.is_hovered());
        CHECK(widget.damage().is_empty()); // ... is NOT a change: no new damage
        CHECK(widget.hover_changes == 1);

        widget.set_hovered(false);
        CHECK(!widget.is_hovered());
        CHECK(!widget.damage().is_empty());
        CHECK(widget.hover_changes == 2);
        CHECK(!widget.last_hovered);

        widget.clear_damage();
        widget.set_hovered(false); // already false
        CHECK(widget.damage().is_empty());
        CHECK(widget.hover_changes == 2);
    }

    // -----------------------------------------------------------------------
    // (2) Press: the per-button state, the damage, the hook.
    // -----------------------------------------------------------------------
    {
        probe_widget_t widget;
        widget.set_geometry(rect_t{ 0, 0, 100, 40 });
        CHECK(!widget.is_pressed(mouse_button_enum::LEFT));
        CHECK(!widget.is_pressed(mouse_button_enum::RIGHT));

        widget.set_pressed(mouse_button_enum::LEFT, true);
        CHECK(widget.is_pressed(mouse_button_enum::LEFT));
        CHECK(!widget.is_pressed(mouse_button_enum::RIGHT)); // the buttons are independent
        CHECK(!widget.damage().is_empty());
        CHECK(widget.press_changes == 1);
        CHECK(widget.last_button == mouse_button_enum::LEFT);
        CHECK(widget.last_pressed);

        widget.clear_damage();
        widget.set_pressed(mouse_button_enum::LEFT, true); // the same value again
        CHECK(widget.damage().is_empty());
        CHECK(widget.press_changes == 1);

        widget.set_pressed(mouse_button_enum::RIGHT, true);
        CHECK(widget.is_pressed(mouse_button_enum::RIGHT));
        CHECK(widget.is_pressed(mouse_button_enum::LEFT)); // LEFT is still held too
        CHECK(widget.press_changes == 2);
        CHECK(widget.last_button == mouse_button_enum::RIGHT);
        CHECK(widget.last_pressed);

        widget.set_pressed(mouse_button_enum::LEFT, false);
        CHECK(!widget.is_pressed(mouse_button_enum::LEFT));
        CHECK(widget.is_pressed(mouse_button_enum::RIGHT));
        CHECK(widget.press_changes == 3);
        CHECK(!widget.last_pressed);

        widget.clear_damage();
        widget.set_pressed(mouse_button_enum::LEFT, false); // already released
        CHECK(widget.damage().is_empty());
        CHECK(widget.press_changes == 3);

        widget.set_pressed(mouse_button_enum::RIGHT, false);
        CHECK(!widget.is_pressed(mouse_button_enum::RIGHT));
        CHECK(widget.press_changes == 4);
    }

    // -----------------------------------------------------------------------
    // (3) Capture: no sink means no capture; a router's sink makes the item the target.
    // -----------------------------------------------------------------------
    {
        probe_widget_t orphan;
        orphan.set_geometry(rect_t{ 0, 0, 50, 20 });
        CHECK(!orphan.capture_pointer()); // nobody would own the pointer
        CHECK(!orphan.captures_pointer());
        orphan.release_pointer(); // a no-op, and no crash
        CHECK(!orphan.captures_pointer());
    }
    {
        probe_widget_t root;
        root.set_geometry(rect_t{ 0, 0, 200, 100 });
        probe_widget_t item{ &root };
        item.set_geometry(rect_t{ 10, 10, 60, 30 });
        input_router_t router{ root };
        CHECK(router.mouse_target() == nullptr);

        CHECK(item.capture_pointer());
        CHECK(item.captures_pointer());
        CHECK(router.mouse_target() == &item);

        // The capture target receives pointer steps whatever the hit test says: (1, 1) is outside
        // the item and inside the root.
        const int interactions_before = item.interactions;
        (void)router.dispatch(move_to(point_t{ 1, 1 }));
        CHECK(item.interactions == interactions_before + 1);
        CHECK(router.mouse_target() == &item);

        item.release_pointer();
        CHECK(!item.captures_pointer());
        CHECK(router.mouse_target() == nullptr); // the router dropped it with the item
        const int interactions_after = item.interactions;
        (void)router.dispatch(move_to(point_t{ 1, 1 }));
        CHECK(item.interactions == interactions_after); // the root is the target again
    }

    // -----------------------------------------------------------------------
    // (4) Routing: a press sets the state and takes the pointer; a release far away still reaches
    //     the owner and then clears both.
    // -----------------------------------------------------------------------
    {
        probe_widget_t root;
        root.set_geometry(rect_t{ 0, 0, 200, 100 });
        probe_widget_t target{ &root };
        target.set_geometry(rect_t{ 10, 10, 60, 30 });
        probe_widget_t other{ &root };
        other.set_geometry(rect_t{ 110, 10, 60, 30 });
        input_router_t router{ root };

        (void)router.dispatch(press_at(point_t{ 20, 20 })); // inside `target`
        CHECK(target.is_hovered());
        CHECK(target.is_pressed(mouse_button_enum::LEFT));
        CHECK(!target.is_pressed(mouse_button_enum::RIGHT));
        CHECK(target.captures_pointer());
        CHECK(router.mouse_target() == &target);
        CHECK(!other.is_pressed(mouse_button_enum::LEFT));

        // Far away, outside every rectangle, with the button still held.
        const int target_interactions = target.interactions;
        const int other_interactions = other.interactions;
        (void)router.dispatch(move_to(point_t{ 400, 300 }));
        CHECK(target.interactions == target_interactions + 1); // the implicit grab delivers
        CHECK(other.interactions == other_interactions);
        CHECK(!target.is_hovered());                            // hover follows the pointer ...
        CHECK(target.is_pressed(mouse_button_enum::LEFT));      // ... the press follows the capture

        // The release far away still reaches the owner; then both the press and the capture clear.
        (void)router.dispatch(release_at(point_t{ 400, 300 }));
        CHECK(target.interactions == target_interactions + 2);
        CHECK(!target.is_pressed(mouse_button_enum::LEFT));
        CHECK(!target.captures_pointer());
        CHECK(router.mouse_target() == nullptr);

        // A press on the other item now works: the pointer was given back.
        (void)router.dispatch(press_at(point_t{ 120, 20 }));
        CHECK(other.is_pressed(mouse_button_enum::LEFT));
        CHECK(other.captures_pointer());
        CHECK(!target.is_pressed(mouse_button_enum::LEFT));
        (void)router.dispatch(release_at(point_t{ 120, 20 }));
        CHECK(!other.captures_pointer());
    }

    // -----------------------------------------------------------------------
    // (5) Hover migration: A -> B -> outside.
    // -----------------------------------------------------------------------
    {
        probe_widget_t root;
        root.set_geometry(rect_t{ 0, 0, 200, 100 });
        probe_widget_t a{ &root };
        a.set_geometry(rect_t{ 10, 10, 60, 30 });
        probe_widget_t b{ &root };
        b.set_geometry(rect_t{ 110, 10, 60, 30 });
        input_router_t router{ root };

        (void)router.dispatch(move_to(point_t{ 20, 20 }));
        CHECK(a.is_hovered());
        CHECK(!b.is_hovered());
        CHECK(!root.is_hovered()); // only the hit item carries hover

        (void)router.dispatch(move_to(point_t{ 120, 20 }));
        CHECK(!a.is_hovered()); // the old target was cleared
        CHECK(b.is_hovered());
        CHECK(!root.is_hovered());

        // The synthetic hover-out is one interaction with hovered == false and no edges.
        CHECK(a.interactions > 0);
        CHECK(!a.last.hovered);
        CHECK(!a.last.mouse_down && !a.last.mouse_pressed && !a.last.mouse_released);

        (void)router.dispatch(move_to(point_t{ 400, 300 })); // outside the tree
        CHECK(!b.is_hovered());
        CHECK(!root.is_hovered());
    }

    // -----------------------------------------------------------------------
    // (6) Disabled and not-visible items never enter hover.
    // -----------------------------------------------------------------------
    {
        probe_widget_t widget;
        widget.set_geometry(rect_t{ 0, 0, 100, 40 });
        widget.set_enabled(false);
        widget.set_hovered(true);
        CHECK(!widget.is_hovered()); // refused: a disabled item cannot be hovered
        CHECK(widget.hover_changes == 0);

        widget.set_enabled(true);
        widget.set_hovered(true);
        CHECK(widget.is_hovered());
        widget.set_hovered(false);

        widget.set_visible(false);
        widget.set_hovered(true);
        CHECK(!widget.is_hovered()); // hidden by its own flag

        probe_widget_t parent;
        parent.set_geometry(rect_t{ 0, 0, 100, 40 });
        probe_widget_t child{ &parent };
        child.set_geometry(rect_t{ 10, 10, 30, 20 });
        parent.set_visible(false);
        child.set_hovered(true);
        CHECK(!child.is_hovered()); // hidden by an ANCESTOR: not visible in the window
    }
    {
        probe_widget_t root;
        root.set_geometry(rect_t{ 0, 0, 200, 100 });
        probe_widget_t hidden{ &root };
        hidden.set_geometry(rect_t{ 10, 10, 60, 30 });
        hidden.set_visible(false);
        probe_widget_t disabled{ &root };
        disabled.set_geometry(rect_t{ 110, 10, 60, 30 });
        disabled.set_enabled(false);
        input_router_t router{ root };

        (void)router.dispatch(move_to(point_t{ 20, 20 })); // over the hidden item
        CHECK(!hidden.is_hovered());
        CHECK(!hidden.is_pressed(mouse_button_enum::LEFT));
        CHECK(root.is_hovered()); // the visible ancestor is the target

        (void)router.dispatch(move_to(point_t{ 120, 20 })); // over the disabled item
        CHECK(!disabled.is_hovered());
        CHECK(root.is_hovered());

        // A press over a disabled item does not press it either: the hit test never reaches it.
        (void)router.dispatch(press_at(point_t{ 120, 20 }));
        CHECK(!disabled.is_pressed(mouse_button_enum::LEFT));
        CHECK(!disabled.captures_pointer());
        CHECK(router.mouse_target() == &root);
        (void)router.dispatch(release_at(point_t{ 120, 20 }));
        CHECK(router.mouse_target() == nullptr);
    }

    // -----------------------------------------------------------------------
    // (7) The paint path reads the ITEM state (a push_button_t paints from it).
    // -----------------------------------------------------------------------
    {
        push_button_t button;
        button.set_geometry(rect_t{ 0, 0, 100, 30 });
        const color_t normal = paint_pixel(button, 2, 2);

        button.set_hovered(true);
        const color_t hovered = paint_pixel(button, 2, 2);
        CHECK(!(normal == hovered)); // the hover colour came from is_hovered()

        button.set_pressed(mouse_button_enum::LEFT, true);
        const color_t pressed = paint_pixel(button, 2, 2);
        CHECK(!(pressed == hovered)); // the pressed colour came from is_pressed()

        button.set_pressed(mouse_button_enum::LEFT, false);
        CHECK((paint_pixel(button, 2, 2) == hovered));

        button.set_hovered(false);
        CHECK((paint_pixel(button, 2, 2) == normal));
    }

    // -----------------------------------------------------------------------
    // (8) The immediate host mirrors its inner hover/press into the control's item state.
    // -----------------------------------------------------------------------
    {
        image_t image{ 240, 160 };
        painter_t painter{ image };
        painter.begin();
        im_host_t host;
        host.set_geometry(rect_t{ 0, 0, 240, 160 });
        host.set_frame_handler([](im_host_t& driven) {
            (void)driven.control(button_type, "ok");
        });
        host.render(painter);
        auto* const button = static_cast<push_button_t*>(host.widget(button_type, "ok"));
        CHECK(button != nullptr);
        CHECK(!button->is_pressed(mouse_button_enum::LEFT));

        // A press pass: the host consumed the press and the button's ITEM state says so.
        host.interact(press_at(point_t{ 10, 13 }));
        host.render(painter);
        CHECK(button->is_hovered());
        CHECK(button->is_pressed(mouse_button_enum::LEFT));
        CHECK(!button->clicked()); // pressing is not clicking

        // Dragging out keeps both the press state and the host's grab, but not the hover.
        host.interact(move_to(point_t{ 500, 500 }));
        host.render(painter);
        CHECK(!button->is_hovered());
        CHECK(button->is_pressed(mouse_button_enum::LEFT));

        // The release arrives outside: the host cancels, and the item state clears with it.
        host.interact(release_at(point_t{ 500, 500 }));
        host.render(painter);
        CHECK(!button->is_pressed(mouse_button_enum::LEFT));
        CHECK(!button->is_hovered());
        CHECK(!button->clicked()); // released outside: no click

        // A full press-release inside clicks exactly once, and the state matches it.
        host.interact(press_at(point_t{ 10, 13 }));
        host.render(painter);
        CHECK(button->is_pressed(mouse_button_enum::LEFT));
        CHECK(!button->clicked());
        host.interact(release_at(point_t{ 10, 13 }));
        host.render(painter);
        CHECK(!button->is_pressed(mouse_button_enum::LEFT));
        CHECK(button->clicked()); // the item state and the host's result agree
        painter.end();
    }

    // -----------------------------------------------------------------------
    // (9) The window adapter clears the pointer state when the mouse leaves the window.
    // -----------------------------------------------------------------------
    {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        hwshqtb::metaobject::core_application application{ argc, argv };
        {
            gui_application_t gui;
            CHECK(gui_application_t::is_video_initialized());

            widget_window_t window{ "item_input_state", extent_t{ 200, 100 } };
            auto* const root = new probe_widget_t{};
            window.set_root_widget(root);
            window.repaint();
            CHECK(!root->is_hovered());

            // A motion through the real native filter chain makes the root the hover target.
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = window.window_id();
            motion.motion.x = 50.0f;
            motion.motion.y = 50.0f;
            CHECK(SDL_PushEvent(&motion));
            CHECK(pump_until([root] {
                return root->is_hovered();
            }));

            // The pointer left the client area: the router (the ONE owner of this state) clears it,
            // and the window repaints the damage the clearing recorded.
            SDL_Event leave{};
            leave.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
            leave.window.windowID = window.window_id();
            CHECK(SDL_PushEvent(&leave));
            CHECK(pump_until([root] {
                return !root->is_hovered();
            }));

            // Focus loss is the other end of the same session: it clears the state too.
            CHECK(SDL_PushEvent(&motion));
            CHECK(pump_until([root] {
                return root->is_hovered();
            }));
            SDL_Event focus_lost{};
            focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            focus_lost.window.windowID = window.window_id();
            CHECK(SDL_PushEvent(&focus_lost));
            CHECK(pump_until([root] {
                return !root->is_hovered();
            }));

            // -----------------------------------------------------------------------
            // (10) A pointer coordinate that is NaN, infinite or far outside the int range must not
            //      be converted to an int: that conversion is undefined behaviour in C++, so before
            //      the guard it could crash or miscompile the comparison it feeds. The saturation
            //      rule the window applies is pinned here, case by case, through the real native
            //      filter (SDL event -> window_t -> input_state_t).
            //
            //      Each case has ONE required answer. NaN -> the origin per axis, because a NaN
            //      carries no position and the origin is the only legal client-area point that needs
            //      no sign; NaN on one axis leaves the other axis alone. +inf and 1e30 saturate to
            //      INT_MAX, -inf and -1e30 to INT_MIN: an infinite coordinate still points somewhere,
            //      so it keeps its direction. The last case is the regression guard: a legitimate
            //      coordinate must land on exactly the pixel it landed on before the guard existed.
            // -----------------------------------------------------------------------
            {
                constexpr float inf = std::numeric_limits<float>::infinity();
                constexpr float nan = std::numeric_limits<float>::quiet_NaN();
                constexpr int int_max = std::numeric_limits<int>::max();
                constexpr int int_min = std::numeric_limits<int>::min();

                struct pointer_case_t {
                    const char* what;
                    float x;
                    float y;
                    point_t expected;
                };
                const pointer_case_t cases[] = {
                    { "NaN x, NaN y", nan, nan, point_t{ 0, 0 } },
                    { "NaN x", nan, 60.0f, point_t{ 0, 60 } },
                    { "NaN y", 30.0f, nan, point_t{ 30, 0 } },
                    { "+inf x", inf, 60.0f, point_t{ int_max, 60 } },
                    { "-inf x", -inf, 60.0f, point_t{ int_min, 60 } },
                    { "+1e30 x", 1e30f, 60.0f, point_t{ int_max, 60 } },
                    { "-1e30 y", 30.0f, -1e30f, point_t{ 30, int_min } },
                    // The regression: unchanged result for a value that always was in range
                    // (truncation towards zero is part of the contract, not a rounding choice).
                    { "100.75 x, 60.75 y", 100.75f, 60.75f, point_t{ 100, 60 } },
                };

                for (const pointer_case_t& c : cases) {
                    SDL_Event motion{};
                    motion.type = SDL_EVENT_MOUSE_MOTION;
                    motion.motion.windowID = window.window_id();
                    motion.motion.x = c.x;
                    motion.motion.y = c.y;
                    CHECK(SDL_PushEvent(&motion));
                    pump_queued();
                    // The EXACT point is required, not a range: a range wide enough to be honest
                    // would also accept the arbitrary value an unguarded conversion produces, which
                    // is exactly what this section has to rule out.
                    const point_t got = window.input_state().mouse_position();
                    CHECK_POINT(c.what, got, c.expected);
                }

                // The same value pushed twice must not drift: the guard is a pure function of the
                // coordinate, so a second NaN event reproduces the origin.
                SDL_Event repeat{};
                repeat.type = SDL_EVENT_MOUSE_MOTION;
                repeat.motion.windowID = window.window_id();
                repeat.motion.x = nan;
                repeat.motion.y = nan;
                CHECK(SDL_PushEvent(&repeat));
                pump_queued();
                CHECK_POINT("NaN repeated", window.input_state().mouse_position(), point_t(0, 0));
            }

            // ---- (11) platform text input follows the keyboard focus ----
            // SDL produces no SDL_EVENT_TEXT_INPUT unless text input is active for the focused window,
            // and it is off by default. The dummy driver generates no focus events, so this injects the
            // two the platform would produce, pumps one round and reads the state back through
            // SDL_TextInputActive. The injected events are the platform's OWN protocol, not a shortcut
            // around it: with the SDL_StartTextInput() call on FOCUS_GAINED removed, (11.1) fails.
            SDL_Window* const sdl_window = SDL_GetWindowFromID(window.window_id());
            CHECK(sdl_window != nullptr);
            CHECK(!SDL_TextInputActive(sdl_window)); // nothing enabled text input before the focus

            // (11.1) focus gained -> text input active.
            SDL_Event focus_gained{};
            focus_gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
            focus_gained.window.windowID = window.window_id();
            CHECK(SDL_PushEvent(&focus_gained));
            CHECK(pump_until([sdl_window] {
                return SDL_TextInputActive(sdl_window);
            }));
            CHECK(SDL_TextInputActive(sdl_window));

            // (11.2) focus lost -> text input inactive again.
            SDL_Event text_focus_lost{};
            text_focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            text_focus_lost.window.windowID = window.window_id();
            CHECK(SDL_PushEvent(&text_focus_lost));
            CHECK(pump_until([sdl_window] {
                return !SDL_TextInputActive(sdl_window);
            }));
            CHECK(!SDL_TextInputActive(sdl_window));

            // (11.3) show() starts it when the window already owns the focus (the dummy driver grants
            // the focus inside SDL_ShowWindow, i.e. before the queued FOCUS_GAINED is pumped), and
            // hide() stops it even though the window may still be the recorded focus.
            window.show();
            CHECK(SDL_TextInputActive(sdl_window));
            window.hide();
            CHECK(!SDL_TextInputActive(sdl_window));

            // (11.4) close() stops it too. close() quits the application, which is a no-op without a
            // running loop; the window is still alive, so the state can be read back.
            CHECK(SDL_PushEvent(&focus_gained));
            CHECK(pump_until([sdl_window] {
                return SDL_TextInputActive(sdl_window);
            }));
            window.close();
            CHECK(!SDL_TextInputActive(sdl_window));
        }
    }

    if (g_failures == 0) {
        std::printf("item input state: all %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("item input state: %d checks, %d failure(s)\n", g_checks, g_failures);
    return 1;
}
